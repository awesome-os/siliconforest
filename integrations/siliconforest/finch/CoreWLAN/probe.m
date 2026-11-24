/* SPDX-License-Identifier: MIT */
#import <CoreWLAN/CoreWLAN.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    @autoreleasepool {
        CWWiFiClient *client = [CWWiFiClient sharedWiFiClient];
        assert(client && !client.interfaces.count && !client.interfaceNames.count && !client.interface);
        CWMutableNetworkProfile *profile = [CWMutableNetworkProfile new];
        NSData *ssid = [@"Finch CLI check" dataUsingEncoding:NSUTF8StringEncoding];
        [profile setSsidData:ssid];
        assert([profile.ssidData isEqual:ssid] && [profile.ssid isEqualToString:@"Finch CLI check"]);
        CWNetworkProfile *copied = [profile copy];
        assert([copied.ssidData isEqual:ssid]);
        assert([CWConfiguration new].rememberJoinedNetworks);
        puts("CoreWLAN: PASS (empty client, profile bytes/string/copy, configuration defaults)");
    }
    return 0;
}
