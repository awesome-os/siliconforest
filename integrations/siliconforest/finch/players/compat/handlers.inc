/* SPDX-License-Identifier: MIT */
/* Appended inside the PD registry source so every registered application counts. */
CFArrayRef LSCopyAllHandlersForURLScheme(CFStringRef scheme) {
 if(!scheme||CFGetTypeID(scheme)!=CFStringGetTypeID()||!CFStringGetLength(scheme))return NULL;
 buildRegistryIfNeeded();
 CFMutableArrayRef result=CFArrayCreateMutable(NULL,0,&kCFTypeArrayCallBacks);if(!result)return NULL;
 for(CFIndex n=0;n<CFArrayGetCount(sApplicationURLs);n++){
  CFBundleRef bundle=CFBundleCreate(NULL,CFArrayGetValueAtIndex(sApplicationURLs,n));if(!bundle)continue;
  CFStringRef identifier=CFBundleGetIdentifier(bundle);CFTypeRef types=CFBundleGetValueForInfoDictionaryKey(bundle,CFSTR("CFBundleURLTypes"));Boolean found=false;
  if(identifier&&types&&CFGetTypeID(types)==CFArrayGetTypeID())for(CFIndex i=0;i<CFArrayGetCount(types)&&!found;i++){
   CFTypeRef entry=CFArrayGetValueAtIndex(types,i);if(!entry||CFGetTypeID(entry)!=CFDictionaryGetTypeID())continue;
   CFTypeRef schemes=CFDictionaryGetValue(entry,CFSTR("CFBundleURLSchemes"));if(!schemes||CFGetTypeID(schemes)!=CFArrayGetTypeID())continue;
   for(CFIndex j=0;j<CFArrayGetCount(schemes);j++){
    CFTypeRef item=CFArrayGetValueAtIndex(schemes,j);
    if(item&&CFGetTypeID(item)==CFStringGetTypeID()&&CFStringCompare(item,scheme,kCFCompareCaseInsensitive)==kCFCompareEqualTo){found=true;break;}
   }
  }
  if(found&&!CFArrayContainsValue(result,CFRangeMake(0,CFArrayGetCount(result)),identifier))CFArrayAppendValue(result,identifier);
  CFRelease(bundle);
 }
 return result;
}
