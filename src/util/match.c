#include "util/match.h"

#include <ctype.h>
#include <stddef.h>

bool pattern_match(const char *pattern, const char *str)
{
    if (!pattern || !str) {
        return false;
    }

    const char *p = pattern;
    const char *s = str;
    const char *p_star = NULL;
    const char *s_star = NULL;

    while (*s != '\0') {
        if (*p == '*') {
            p_star = p++;
            s_star = s;
        } else if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*s)) {
            p++;
            s++;
        } else if (p_star != NULL) {
            p = p_star + 1;
            s = ++s_star;
        } else {
            return false;
        }
    }

    while (*p == '*') {
        p++;
    }

    return *p == '\0';
}
