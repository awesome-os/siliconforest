/* Cases for the extracted adapter bodies (appended after them by the runner). */
static std::vector<uint8_t> info(uint8_t iface, uint16_t input, size_t n = 16)
{
    std::vector<uint8_t> v(n, 0);
    v[1] = iface; v[5] = 33;
    v[6] = input & 0xff; v[7] = input >> 8; v[8] = 2; v[10] = 0xf6; v[11] = 0x0f;
    if (n > 16) v[16] = 'X';  /* a name span must never be copied */
    return v;
}
static void preKeyboardClaims(PDM1SPIHID &d)
{
    assert(m1tp_claim_request(&d.usedRequestKeys, 0x01, 0xd0));
    assert(m1tp_claim_request(&d.usedRequestKeys, 0x02, 0x01));
    assert(m1tp_claim_request(&d.usedRequestKeys, 0x10, 0x01));
}
static struct m1tp_observation stored(const IOService &s)
{
    struct m1tp_observation o;
    auto it = s.props.find(M1TP_OBSERVATION_KEY);
    assert(it != s.props.end() && it->second.size() == sizeof(o));
    memcpy(&o, it->second.data(), sizeof(o));
    return o;
}

int main()
{
    unsigned cases = 0;
    {   /* success: two read queries in order, header-only copy, descriptor owned */
        PDM1SPIHID d; preKeyboardClaims(d);
        std::vector<uint8_t> desc(90); for (size_t i = 0; i < desc.size(); i++) desc[i] = (uint8_t)(i * 7);
        d.script = {{true, info(2, 0x200, 24)}, {true, desc}};
        assert(d.enumerateTrackpad());
        assert((d.requests == std::vector<std::pair<unsigned, unsigned>>{{2, 2}, {0x10, 2}}));
        const struct m1tp_observation &o = d.trackpadEnumeration;
        assert(o.result == M1TP_OBS_ENUM_OK && o.stage == M1TP_OBS_COMPLETE);
        assert(o.metadata_payload_bytes == 24 && o.descriptor_payload_bytes == 90 && o.descriptor_bytes == 90);
        assert(o.metadata_header[1] == 2 && !memchr(o.metadata_header, 'X', 16));
        assert(!memcmp(o.descriptor, desc.data(), 90) && o.descriptor[90] == 0);
        assert(d.trackpadInterface.interface == 2 && d.trackpadInterface.max_input == 0x200 && d.fails.empty());
        assert(d.usedRequestKeys == M1TP_CLAIM_MASK);
        cases++;
    }
    {   /* info request failure */
        PDM1SPIHID d; preKeyboardClaims(d); d.script = {{false, {}}};
        assert(!d.enumerateTrackpad());
        assert(d.trackpadEnumeration.result == M1TP_OBS_INFO_REQUEST_FAILED && d.trackpadEnumeration.stage == M1TP_OBS_INFO_REQUEST);
        assert(d.requests.size() == 1 && d.trackpadEnumeration.metadata_payload_bytes == 0);
        cases++;
    }
    {   /* info invalid (keyboard interface number): header kept, no descriptor request */
        PDM1SPIHID d; preKeyboardClaims(d); d.script = {{true, info(1, 0x200)}};
        assert(!d.enumerateTrackpad());
        assert(d.trackpadEnumeration.result == M1TP_OBS_INFO_INVALID && d.trackpadEnumeration.stage == M1TP_OBS_INFO_RECEIVED);
        assert(d.requests.size() == 1 && d.trackpadEnumeration.metadata_header[1] == 1);
        assert(d.fails.size() == 1 && d.fails[0] == "trackpad-interface-metadata");
        cases++;
    }
    {   /* short metadata: partial raw header, invalid */
        PDM1SPIHID d; preKeyboardClaims(d); d.script = {{true, std::vector<uint8_t>{0, 2, 0}}};
        assert(!d.enumerateTrackpad());
        assert(d.trackpadEnumeration.result == M1TP_OBS_INFO_INVALID && d.trackpadEnumeration.metadata_payload_bytes == 3);
        assert(d.trackpadEnumeration.metadata_header[1] == 2 && d.trackpadEnumeration.metadata_header[3] == 0);
        cases++;
    }
    {   /* descriptor request failure */
        PDM1SPIHID d; preKeyboardClaims(d); d.script = {{true, info(2, 64)}, {false, {}}};
        assert(!d.enumerateTrackpad());
        assert(d.trackpadEnumeration.result == M1TP_OBS_DESC_REQUEST_FAILED && d.trackpadEnumeration.stage == M1TP_OBS_DESC_REQUEST);
        cases++;
    }
    for (size_t n : {size_t(0), size_t(513), size_t(2038)}) {  /* descriptor invalid: payload length kept, nothing stored */
        PDM1SPIHID d; preKeyboardClaims(d); d.script = {{true, info(2, 64)}, {true, std::vector<uint8_t>(n, 0xa5)}};
        assert(!d.enumerateTrackpad());
        const struct m1tp_observation &o = d.trackpadEnumeration;
        assert(o.result == M1TP_OBS_DESC_INVALID && o.stage == M1TP_OBS_DESC_RECEIVED);
        assert(o.descriptor_payload_bytes == n && o.descriptor_bytes == 0 && o.descriptor[0] == 0);
        assert(d.fails.back() == "trackpad-descriptor-length");
        cases++;
    }
    {   /* a used trackpad key is never re-requested */
        PDM1SPIHID d; preKeyboardClaims(d); d.usedRequestKeys |= M1TP_BIT_IFACE2;
        assert(!d.enumerateTrackpad() && d.trackpadEnumeration.result == M1TP_OBS_INFO_REQUEST_FAILED && d.script.empty());
        cases++;
    }
    {   /* handleStart: snapshot preserved on provider and instance after a failed gate */
        PDM1SPIHID d; IOService provider; preKeyboardClaims(d);
        d.gate.result = -1;
        assert(!d.handleStart(&provider) && d.gate.calls == 1);
        struct m1tp_observation p = stored(provider), s = stored(d);
        assert(p.result == M1TP_OBS_NOT_ATTEMPTED && p.stage == M1TP_OBS_BEFORE_IO && p.version == 1);
        assert(p.request_mask == 7 && !memcmp(&p, &s, sizeof(p)) && OSData::live == 0);
        cases++;
    }
    {   /* handleStart: success path copies the complete snapshot, mask from used keys */
        PDM1SPIHID d; IOService provider;
        d.gate.hook = [&] { preKeyboardClaims(d); std::vector<uint8_t> desc(64, 1);
                            d.script = {{true, info(2, 64)}, {true, desc}}; assert(d.enumerateTrackpad()); };
        assert(d.handleStart(&provider));
        struct m1tp_observation p = stored(provider);
        assert(p.result == M1TP_OBS_ENUM_OK && p.request_mask == 31 && p.descriptor_bytes == 64);
        cases++;
    }
    {   /* handleStart: a failed enumeration is still preserved with its raw result */
        PDM1SPIHID d; IOService provider;
        d.gate.result = -1;
        d.gate.hook = [&] { preKeyboardClaims(d); d.script = {{true, info(2, 64)}, {true, std::vector<uint8_t>(600, 2)}};
                            assert(!d.enumerateTrackpad()); };
        assert(!d.handleStart(&provider));
        struct m1tp_observation p = stored(provider);
        assert(p.result == M1TP_OBS_DESC_INVALID && p.descriptor_payload_bytes == 600 && p.descriptor_bytes == 0);
        cases++;
    }
    {   /* base handleStart failure: no gate, NOT_ATTEMPTED still preserved */
        PDM1SPIHID d; IOService provider; d.handleStartResult = false;
        assert(!d.handleStart(&provider) && d.gate.calls == 0 && stored(provider).result == M1TP_OBS_NOT_ATTEMPTED);
        cases++;
    }
    {   /* copy failures fail start without changing the raw result */
        PDM1SPIHID d; IOService provider; OSData::failNext = true;
        assert(!d.handleStart(&provider) && provider.props.empty() && d.props.empty());
        OSData::failNext = false;
        assert(d.trackpadEnumeration.result == M1TP_OBS_NOT_ATTEMPTED);
        cases++;
    }
    {
        PDM1SPIHID d; IOService provider; provider.failSet = true;
        assert(!d.handleStart(&provider) && provider.props.empty() && d.props.count(M1TP_OBSERVATION_KEY));
        cases++;
    }
    {   /* instance failure: provider copy survives, start fails */
        PDM1SPIHID d; IOService provider; d.failSet = true;
        assert(!d.handleStart(&provider) && provider.props.count(M1TP_OBSERVATION_KEY) && d.props.empty());
        cases++;
    }
    assert(OSData::live == 0);
    printf("HOST_TRACKPAD_ENUM_ADAPTER cases=%u hardware=none\n", cases);
    return 0;
}
