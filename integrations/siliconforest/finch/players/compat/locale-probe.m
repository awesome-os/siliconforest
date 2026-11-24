/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#include <stdio.h>
@interface NSLocale(SFDirectionProbe)
+ (NSUInteger)characterDirectionForLanguage:(NSString *)language;
+ (NSUInteger)lineDirectionForLanguage:(NSString *)language;
@end
int main(void){@autoreleasepool {
 if(![NSLocale respondsToSelector:@selector(characterDirectionForLanguage:)]||![NSLocale respondsToSelector:@selector(lineDirectionForLanguage:)])return 2;
 NSString *languages[]={@"en",@"de",@"ar",@"he",@"fa",@"ur",@"ja",@"ko",@"zh-Hant",@"mn",@"bo",@"syr",@"und",@"",@"zz",@"en-US"};
 for(unsigned i=0;i<sizeof(languages)/sizeof(languages[0]);i++)printf("LOCALE case=%u character=%lu line=%lu\n",i,(unsigned long)[NSLocale characterDirectionForLanguage:languages[i]],(unsigned long)[NSLocale lineDirectionForLanguage:languages[i]]);
 return 0;
}}
