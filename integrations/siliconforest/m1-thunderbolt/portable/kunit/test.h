/* SPDX-License-Identifier: MIT */
/* Host assertion adapter for the unmodified Linux test cases. */
#ifndef SF_TB_KUNIT_TEST_H
#define SF_TB_KUNIT_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <linux/types.h>
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
struct kunit { const char *name; unsigned long assertions; };
struct kunit_case { const char *name; void (*run)(struct kunit *); };
struct kunit_suite { const char *name; struct kunit_case *test_cases; };
#define SF_EXPECT(t,condition) do { ++(t)->assertions; if (!(condition)) { fprintf(stderr,"%s:%d %s: %s\n",__FILE__,__LINE__,(t)->name,#condition); abort(); } } while(0)
#define KUNIT_EXPECT_EQ(t,a,b) SF_EXPECT(t,(a)==(b))
#define KUNIT_EXPECT_PTR_EQ(t,a,b) SF_EXPECT(t,(a)==(b))
#define KUNIT_EXPECT_TRUE(t,x) SF_EXPECT(t,(x))
#define KUNIT_EXPECT_FALSE(t,x) SF_EXPECT(t,!(x))
#define KUNIT_ASSERT_EQ KUNIT_EXPECT_EQ
#define KUNIT_ASSERT_TRUE KUNIT_EXPECT_TRUE
#define KUNIT_EXPECT_EQ_MSG(t,a,b,...) KUNIT_EXPECT_EQ(t,a,b)
#define KUNIT_ASSERT_EQ_MSG(t,a,b,...) KUNIT_EXPECT_EQ(t,a,b)
#define KUNIT_CASE(fn) {#fn,fn}
#define kunit_test_suite(suite) \
 int main(void) { unsigned n=0; unsigned long assertions=0; \
 for(struct kunit_case *c=(suite).test_cases;c->run;++c) { \
 struct kunit t={c->name,0};c->run(&t);assertions+=t.assertions;++n; } \
 printf("%s: %u upstream cases, %lu assertions passed (host fixtures)\n",(suite).name,n,assertions);return 0; }
#endif
