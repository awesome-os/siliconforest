// SPDX-License-Identifier: MIT
// sf-winresize-probe.dylib (TERM6): inserted into an application (DYLD_INSERT_LIBRARIES) to time window resizes as an
// interactive resize makes them, without a person's corner drag (the desk's resize grab cannot be started by synthetic
// input): SFWR_DELAY seconds after launch (default 9), the window whose title begins with SFWR_TITLE (default the main window)
// is made SFWR_STEP points wider (default 8) SFWR_STEPS times (default 30) and narrower again, one step each
// SFWR_EVERY ms (default 50); each step is -setFrame:display:YES and -displayIfNeeded, timed; one line per step and
// a summary go to stderr ("WINRESIZE ..."). Nothing else is changed.
#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double sfwrNow(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static double sfwrEnv(const char *name, double fallback)
{
	const char *v = getenv(name);

	return v ? atof(v) : fallback;
}

static NSWindow *sfwrWindow(void)
{
	const char *title = getenv("SFWR_TITLE");

	if (title == NULL)
		return [NSApp mainWindow] ? [NSApp mainWindow] : [NSApp keyWindow];
	for (NSWindow *w in [NSApp windows])
		if ([[w title] hasPrefix:[NSString stringWithUTF8String:title]] && [w isVisible])
			return w;
	return nil;
}

static int sfwrCompare(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static void sfwrStep(int i, int steps, double step, double every, double *times)
{
	NSWindow *w = sfwrWindow();

	if (w == nil) {
		fprintf(stderr, "WINRESIZE no window\n");
		return;
	}
	if (i == 2 * steps) {
		qsort(times, 2 * steps, sizeof(double), sfwrCompare);
		fprintf(stderr, "WINRESIZE done steps %d median %.1f p95 %.1f max %.1f ms\n", 2 * steps, times[steps],
		        times[(int)(0.95 * (2 * steps - 1))], times[2 * steps - 1]);
		free(times);
		return;
	}
	NSRect f = [w frame];

	f.size.width += i < steps ? step : -step;
	double t0 = sfwrNow();
	[w setFrame:f display:YES];
	[w displayIfNeeded];
	double t1 = sfwrNow();
	times[i] = t1 - t0;
	fprintf(stderr, "WINRESIZE step %d width %.0f %.1f ms\n", i, f.size.width, t1 - t0);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(every * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
		sfwrStep(i + 1, steps, step, every, times);
	});
}

__attribute__((constructor)) static void sfwrStart(void)
{
	double delay = sfwrEnv("SFWR_DELAY", 9), step = sfwrEnv("SFWR_STEP", 8), every = sfwrEnv("SFWR_EVERY", 50);
	int steps = (int)sfwrEnv("SFWR_STEPS", 30);

	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		sfwrStep(0, steps, step, every, calloc(2 * steps, sizeof(double)));
	});
}
