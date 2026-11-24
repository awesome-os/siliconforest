#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Combine source methods and own model declarations; never reads SDK metadata."""
from pathlib import Path
import re
import subprocess
import sys
here = Path(__file__).resolve().parent
source = Path(sys.argv[1])
text = (source / 'Intents.m').read_text()
classes = dict.fromkeys(re.findall(r'@implementation (IN\w+)', text), 'NSObject')
classes.update(INAnswerCallIntent='INIntent', INAnswerCallIntentResponse='INIntentResponse')
# Base classes must be declared before their subclasses; forward declarations are
# sufficient for object-valued properties and methods.
classes = {n: classes[n] for n in sorted(classes, key=lambda n: classes[n] != 'NSObject')}
out = here / 'include/Intents/Intents.h'
subprocess.run([sys.executable,str(here.parent / 'tools/declarations.py'),str(source),str(here / 'types.h'),str(out),'--categories','--class-map',','.join(n+':'+b for n,b in classes.items())],check=True)
header = out.read_text().replace('#pragma once\n', '#pragma once\n@class INIntentResponse, INBooleanResolutionResult, INSendMessageAttachment, INRelevantShortcut, INVoiceShortcut;\n')
# Source-specific ABI fields, also consumed by Finch's runtime accessor generator.
properties = {
'INPersonHandle': '''@property(copy) NSString *value;
@property INPersonHandleType type;
@property(copy) NSString *label;
- (instancetype)initWithValue:(NSString *)value type:(INPersonHandleType)type label:(NSString *)label;''',
'INPerson': '''@property(copy) NSString *displayName;
@property(copy) INPersonHandle *personHandle;
@property(copy) NSPersonNameComponents *nameComponents;
@property(copy) NSString *contactIdentifier;
@property(copy) NSString *customIdentifier;
- (instancetype)initWithPersonHandle:(INPersonHandle *)personHandle nameComponents:(NSPersonNameComponents *)nameComponents displayName:(NSString *)displayName image:(INImage *)image contactIdentifier:(NSString *)contactIdentifier customIdentifier:(NSString *)customIdentifier;''',
'INObject': '''@property(copy) NSString *identifier;
@property(copy) NSString *displayString;
@property(copy) NSString *subtitleString;
@property(copy) NSString *pronunciationHint;
- (instancetype)initWithIdentifier:(NSString *)identifier displayString:(NSString *)displayString subtitleString:(NSString *)subtitleString displayImage:(INImage *)displayImage;''',
'INAirline': '''@property(copy) NSString *name;
@property(copy) NSString *iataCode;
@property(copy) NSString *icaoCode;
- (instancetype)initWithName:(NSString *)name iataCode:(NSString *)iataCode icaoCode:(NSString *)icaoCode;''',
'INSpeakableString': '''@property(copy) NSString *vocabularyIdentifier;
@property(copy) NSString *spokenPhrase;
@property(copy) NSString *pronunciationHint;
- (instancetype)initWithVocabularyIdentifier:(NSString *)vocabularyIdentifier spokenPhrase:(NSString *)spokenPhrase pronunciationHint:(NSString *)pronunciationHint;''',
'INAnswerCallIntent': '''@property INCallAudioRoute audioRoute;
@property(copy) NSString *callIdentifier;
- (instancetype)initWithAudioRoute:(INCallAudioRoute)audioRoute callIdentifier:(NSString *)callIdentifier;''',
'INAnswerCallIntentResponse': '''@property INAnswerCallIntentResponseCode code;
@property(copy) NSArray *callRecords;
- (instancetype)initWithCode:(INAnswerCallIntentResponseCode)code userActivity:(NSUserActivity *)userActivity;''',
'INFocusStatus': '''@property(copy) NSNumber *isFocused;
- (instancetype)initWithIsFocused:(NSNumber *)isFocused;''',
'INIntent': '@property(copy) NSString *suggestedInvocationPhrase;',
'INInteraction': '''@property(copy) NSDateInterval *dateInterval;
@property(copy) INIntent *intent;
@property(copy) INIntentResponse *intentResponse;
@property(copy) NSString *identifier;
@property(copy) NSString *groupIdentifier;
@property INIntentHandlingStatus intentHandlingStatus;
@property INInteractionDirection direction;''',
'INPaymentMethod': '@property INPaymentMethodType type;',
'INFile': '@property(readonly, copy) NSString *filename, *typeIdentifier;',
'INShortcut': '''@property(copy) NSUserActivity *userActivity;
@property(copy) INIntent *intent;
- (instancetype)initWithUserActivity:(NSUserActivity *)userActivity;
- (instancetype)initWithIntent:(INIntent *)intent;''',
}
for name, body in properties.items():
    header = header.replace('@interface '+name+' : '+classes[name]+'\n', '@interface '+name+' : '+classes[name]+'\n'+body+'\n')
models = (here / 'models.h').read_text()
# INIntentResponse must be complete before its concrete subclass.
base, rest = models.split('@interface INIntentResponse',1)
response, rest = rest.split('@end',1)
header = header.replace('@interface INAnswerCallIntentResponse', '@interface INIntentResponse'+response+'@end\n\n@interface INAnswerCallIntentResponse')
header += '\n'+base+rest
for name in set(classes) - {'INPreferences', 'INVoiceShortcutCenter', 'INRelevantShortcutStore', 'INFocusStatusCenter'}:
    header = header.replace('@interface '+name+' : '+classes[name]+'\n', '@interface '+name+' : '+classes[name]+' <NSCopying, NSSecureCoding>\n')
header += '\n@interface NSString (SFFinchIntentsLocalization)\n+ (NSString *)deferredLocalizedIntentsStringWithFormat:(NSString *)format, ...;\n+ (NSString *)deferredLocalizedIntentsStringWithFormat:(NSString *)format fromTable:(NSString *)table, ...;\n+ (NSString *)deferredLocalizedIntentsStringWithFormat:(NSString *)format fromTable:(NSString *)table arguments:(va_list)arguments;\n@end\n'
out.write_text(header)
