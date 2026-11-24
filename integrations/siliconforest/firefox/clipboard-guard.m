/* SPDX-License-Identifier: MIT
 * Keep the previous clipboard's types and bytes in RAM, restore only while the
 * clipboard still contains this test's own unique text. Never print its data.
 */
#import <AppKit/AppKit.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argc,char **argv){@autoreleasepool{
 if(argc!=3)return 2;setvbuf(stdout,NULL,_IOLBF,0);
 /* wl_data_device only gives selection offers to a focused window. This
  * windowless helper cannot snapshot the desktop selection; its ownership
  * also ends on exit. Refuse the shared desktop until a complete lifetime
  * strategy is qualified. An explicitly isolated seat may test the helper. */
 const char *isolated=getenv("SFFX_CLIPBOARD_ISOLATED");
 if(!isolated||strcmp(isolated,"1")){puts("{\"ready\":false,\"reason\":\"shared-selection-lifetime-unqualified\"}");return 3;}
 NSString *copy=[NSString stringWithUTF8String:argv[1]],*paste=[NSString stringWithUTF8String:argv[2]];
 NSPasteboard *pb=[NSPasteboard generalPasteboard];NSArray *types=[[pb types] copy];
 NSMutableDictionary *saved=[NSMutableDictionary dictionary];NSUInteger bytes=0;
 for(NSString *type in types){NSData *data=[pb dataForType:type];
  if(!data||[data length]>1024*1024||bytes+[data length]>1024*1024){puts("{\"ready\":false,\"reason\":\"snapshot-unavailable-or-large\"}");return 3;}
  bytes+=[data length];[saved setObject:data forKey:type];}
 NSInteger original=[pb changeCount];puts("{\"ready\":true}");char command[80];int wrote=0;
 while(fgets(command,sizeof(command),stdin)){
  if(!strcmp(command,"check-copy\n")){
   NSString *got=[pb stringForType:@"public.utf8-plain-text"];
   if(!got)got=[pb stringForType:NSStringPboardType];
   printf("{\"copy_matches\":%s}\n",[got isEqualToString:copy]?"true":"false");
  }else if(!strcmp(command,"write-paste\n")){
   [pb declareTypes:[NSArray arrayWithObject:@"public.utf8-plain-text"] owner:nil];
   wrote=[pb setString:paste forType:@"public.utf8-plain-text"];
   printf("{\"written\":%s}\n",wrote?"true":"false");
  }else if(!strcmp(command,"restore\n"))break;
  else{puts("{\"error\":\"unknown-command\"}");}
 }
 NSString *current=[pb stringForType:@"public.utf8-plain-text"];
 if(!current)current=[pb stringForType:NSStringPboardType];
 int owned=[current isEqualToString:copy]||[current isEqualToString:paste];
 int restored=0,unchanged=[pb changeCount]==original;
 if(owned){[pb declareTypes:types owner:nil];restored=1;
  for(NSString *type in types)restored&=[pb setData:[saved objectForKey:type] forType:type];
  for(NSString *type in types)restored&=[[pb dataForType:type] isEqualToData:[saved objectForKey:type]];
 }
 printf("{\"restored\":%s,\"unchanged\":%s,\"foreign_change_preserved\":%s}\n",restored?"true":"false",unchanged?"true":"false",!owned&&!unchanged?"true":"false");
 [types release];return (restored||unchanged)?0:4;
}}
