/* SPDX-License-Identifier: GPL-2.0-or-later
 * The boards' sample month for the tests and the renders (GlassineOS/ui designs/planner
 * data/planner.js): March of a year whose 1st is a Monday, today Thursday 11. Every entry is
 * made up and its ink synthetic (seeded loops, never anyone's writing). */
#ifndef PL_SAMPLE_H
#define PL_SAMPLE_H
#include "../src/store.h"

#define SAMPLE_TODAY "2027-03-11"
#define SAMPLE_NOW 860 /* 14:20 */

/* synthetic ink for text on a line at baseline y from x, in the field's coordinates */
void sample_ink(struct field *f, const char *text, double x, double baseline, unsigned seed);
void sample_fill(struct store *s);

#endif
