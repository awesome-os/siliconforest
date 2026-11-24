# SPDX-License-Identifier: MIT
"""Read Gecko's actual font faces in an owned Marionette content document.

InspectorUtils and the named system sandbox are present in the exact Firefox160
source. This is bounded diagnostic metadata, not proof of correct glyph pixels.
No page styles, font preferences or native input are changed.
"""


def observe(client):
    script = '''
      if (typeof InspectorUtils === "undefined")
        return {supported:false,error:"InspectorUtils unavailable"};
      const rows=[], walk=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
      let node;
      while ((node=walk.nextNode()) && rows.length<80) {
        if (!node.textContent.trim() || !node.parentElement) continue;
        const range=document.createRange();range.selectNodeContents(node);
        const box=range.getBoundingClientRect(),style=getComputedStyle(node.parentElement);
        if (box.width<2 || box.height<2 || box.bottom<0 || box.top>innerHeight ||
            box.right<0 || box.left>innerWidth || style.visibility!=="visible") continue;
        const faces=InspectorUtils.getUsedFontFaces(range,4).map(face=>({
          name:face.name,cssFamily:face.CSSFamilyName,cssGeneric:face.CSSGeneric,
          postScript:face.getNameString(6),family:face.getNameString(1),
          subfamily:face.getNameString(2),fromFontGroup:face.fromFontGroup,
          fromLanguagePrefs:face.fromLanguagePrefs,fromSystemFallback:face.fromSystemFallback,
          downloaded:face.rule!==null,localName:face.localName,format:face.format,
          fragments:face.ranges.map(r=>r.toString().slice(0,120))
        }));
        rows.push({text:node.textContent.trim().slice(0,120),cssFamily:style.fontFamily,
          fontVariantCaps:style.fontVariantCaps,fontFeatureSettings:style.fontFeatureSettings,
          fontVariationSettings:style.fontVariationSettings,faces});
      }
      return {supported:true,url:location.href,rows};
    '''
    try:
        return client.command('WebDriver:ExecuteScript', dict(script=script, args=[],
            sandbox='system', newSandbox=True))
    except Exception as error:
        # Lack of diagnostic access is not evidence that font activation failed.
        return dict(supported=False, error=str(error), glyph_pixels_qualified=False)
