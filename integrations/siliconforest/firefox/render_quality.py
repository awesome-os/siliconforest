# SPDX-License-Identifier: MIT
"""Controlled ICC pixels plus glyph raster evidence, not physical panel proof."""
import base64
import json


def measure(client, output, colour_mode):
    client.command('Marionette:SetContext', {'value': 'content'})
    for handle in client.command('WebDriver:GetWindowHandles'):
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        ready = client.command('WebDriver:ExecuteScript', dict(script='return typeof fxQualityPixels === "function";', args=[], newSandbox=False))
        if ready:
            break
    else:
        raise RuntimeError('Owned colour/glyph fixture not found')
    client.command('WebDriver:SetTimeouts', {'script': 12000})
    result = client.command('WebDriver:ExecuteAsyncScript', dict(script='''
      const done=arguments[arguments.length-1];fxQualityPixels().then(done,error=>done({error:String(error)}));''', args=[], newSandbox=False))
    encoded = result.pop('glyphImage', None)
    if encoded:
        output.with_name('glyphs.png').write_bytes(base64.b64decode(encoded.split(',', 1)[1], validate=True))
    expected = 128 if colour_mode == 0 else 188
    result.update(controlled_colour_mode=colour_mode, controlled_srgb_display_profile=True,
                  expected_linear_rgb=expected, glyph_coverage_automatically_qualified=False)
    result['complete'] = bool(not result.get('error') and
        all(abs(value - expected) <= 1 for value in result.get('colour', {}).get('linear', [])[:3]) and
        result.get('colour', {}).get('linear', [None] * 4)[3] == 255 and
        result.get('colour', {}).get('srgb') == [128, 128, 128, 255])
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result
