// SPDX-License-Identifier: MIT
// Probe for /usr/lib/libexpat.1.dylib: parses one small document. A libexpat that exports
// nothing (the defect of 13 November 2026) makes dyld refuse this program.
#include <stdio.h>
#include <string.h>

typedef struct XML_ParserStruct *XML_Parser;
extern XML_Parser XML_ParserCreate(const char *encoding);
extern void XML_SetElementHandler(XML_Parser p, void (*start)(void *, const char *, const char **), void (*end)(void *, const char *));
extern void XML_SetUserData(XML_Parser p, void *data);
extern int XML_Parse(XML_Parser p, const char *s, int len, int isFinal);
extern void XML_ParserFree(XML_Parser p);
extern const char *XML_ExpatVersion(void);

static void start(void *data, const char *name, const char **atts)
{
    if (!strcmp(name, "b") && atts[0] && !strcmp(atts[0], "k") && !strcmp(atts[1], "v")) ++*(int *)data;
}
static void end(void *data, const char *name) { (void)data; (void)name; }

int main(void)
{
    static const char doc[] = "<a><b k=\"v\"/><b k=\"v\"/></a>", broken[] = "<a><b></a>";
    int seen = 0;
    XML_Parser p = XML_ParserCreate(NULL);
    if (!p) { printf("SFEXPAT FAIL no parser\n"); return 1; }
    XML_SetUserData(p, &seen);
    XML_SetElementHandler(p, start, end);
    int ok = XML_Parse(p, doc, (int)sizeof(doc) - 1, 1);
    XML_ParserFree(p);
    p = XML_ParserCreate(NULL);
    int bad = XML_Parse(p, broken, (int)sizeof(broken) - 1, 1);
    XML_ParserFree(p);
    printf("SFEXPAT %s %s: parse=%d elements=%d, a malformed document: parse=%d\n", ok == 1 && seen == 2 && bad == 0 ? "ok" : "FAIL",
           XML_ExpatVersion(), ok, seen, bad);
    return ok == 1 && seen == 2 && bad == 0 ? 0 : 1;
}
