/* SPDX-License-Identifier: MIT */
/* CLI language checks only: no application targets, dialogs, shell or files. */
#import "ASInterpreter.h"
#include <stdio.h>
int main(void) { @autoreleasepool {
    NSArray *cases=@[
      @[@"return 2 + 3 * 4", @"14"],
      @[@"set total to 0\nrepeat with n from 1 to 5\nset total to total + n\nend repeat\nreturn total", @"15"],
      @[@"on twice(x)\nreturn x * 2\nend twice\nreturn twice(9)", @"18"],
      @[@"set xs to {3, 5, 8}\nreturn item 2 of xs", @"5"],
      @[@"set r to {name:\"oak\", age:7}\nreturn age of r", @"7"],
      @[@"if 5 > 3 then\nreturn \"yes\"\nelse\nreturn \"no\"\nend if", @"\"yes\""],
      @[@"return \"silicon\" & \"forest\"", @"\"siliconforest\""]
    ];
    for (NSArray *c in cases) {
      @try {
        ASScript *s=[ASScript compile:c[0]];
        ASInterpreter *interpreter=[ASInterpreter new];
        id result=[interpreter run:s];
        NSString *text=ASDisplay(result);
        if(![text isEqualToString:c[1]]) {
          fprintf(stderr,"FAIL source=%s expected=%s got=%s\n",[c[0] UTF8String],[c[1] UTF8String],text.UTF8String);return 1;
        }
      } @catch(NSException *exception) {
        fprintf(stderr,"FAIL exception=%s reason=%s\n",exception.name.UTF8String,exception.reason.UTF8String);return 1;
      }
    }
    BOOL caught=NO;
    @try {[ASScript compile:@"set x to"];}
    @catch(ASError *error) { caught=YES; }
    if(!caught){fputs("FAIL invalid syntax accepted\n",stderr);return 1;}
    puts("PASS AppleScript arithmetic/loop/handler/list/record/conditional/text/syntax error");
    return 0;
} }
