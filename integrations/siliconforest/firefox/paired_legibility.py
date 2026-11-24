# SPDX-License-Identifier: MIT
"""Font-on/off paint controls in one owned profile, preserving site cookies."""
import json
from urllib.parse import urlsplit

from font_legibility import measure as measure_pages


def measure(client, directory, seat_activity=False):
    key = 'gfx.downloadable_fonts.enabled'
    def chrome(source):
        client.command('Marionette:SetContext', {'value': 'chrome'})
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=[], newSandbox=False))
    previous = chrome('return {user:Services.prefs.prefHasUserValue("'+key+'"),value:Services.prefs.getBoolPref("'+key+'",true)};')
    result = dict(complete=False, same_owned_profile=True, phases=[], legibility_qualified=False)
    try:
        for label, enabled in [('on', True), ('off', False)]:
            actual = chrome('Services.prefs.setBoolPref("'+key+'",'+str(enabled).lower()+');return Services.prefs.getBoolPref("'+key+'");')
            if actual is not enabled:
                raise RuntimeError('Requested downloadable-font preference was not selected')
            client.command('Marionette:SetContext', {'value': 'content'})
            handles = client.command('WebDriver:GetWindowHandles')
            for handle in handles:
                client.command('WebDriver:SwitchToWindow', {'handle': handle})
                if urlsplit(client.command('WebDriver:GetCurrentURL')).scheme in ['http', 'https', 'file']:
                    client.command('WebDriver:Refresh')
            measured = measure_pages(client, directory, handles=handles,
                                     prefix='legibility-'+label, seat_activity=seat_activity)
            result['phases'].append(dict(mode=label, actual_preference=actual, **measured))
        result['complete'] = all(phase['complete'] for phase in result['phases'])
    finally:
        restore = ('Services.prefs.setBoolPref("'+key+'",'+str(previous['value']).lower()+');'
                   if previous['user'] else 'Services.prefs.clearUserPref("'+key+'");')
        result['restored_preference'] = chrome(restore+'return {user:Services.prefs.prefHasUserValue("'+key+'"),value:Services.prefs.getBoolPref("'+key+'",true)};')
        if result['restored_preference'] != previous:
            raise RuntimeError('Owned diagnostic preference was not restored')
        (directory/'paired-legibility.json').write_text(json.dumps(result, indent=2)+'\n')
    return result
