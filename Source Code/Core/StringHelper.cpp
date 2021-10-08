#include "stdafx.h"

#include "Headers/StringHelper.h"

namespace Divide::Util {

bool FindCommandLineArgument(const int argc, char** argv, const char* target_arg, const char* arg_prefix) {
    string tempArg(arg_prefix);
    tempArg += target_arg;
    const char* target = tempArg.c_str();

    for (int i = 0; i < argc; ++i) {
        if (_strcmpi(argv[i], target) == 0) {
            return true;
        }
    }
    return false;
}

bool IsNumber(const char* s) {
    F32 number = 0.0f;
    if (istringstream(s) >> number) {
        return !(IS_ZERO(number) && s[0] != 0);
    }
    return false;
}

void CStringRemoveChar(char* str, const char charToRemove) noexcept {
    char *pr = str, *pw = str;
    while (*pr) {
        *pw = *pr++;
        pw += *pw != charToRemove;
    }
    *pw = '\0';
}

bool IsEmptyOrNull(const char* str) noexcept {
    return str == nullptr || str[0] == '\0';
}

//ref: http://c-faq.com/stdio/commaprint.html
char *commaprint(U64 number) noexcept {
    static int comma = '\0';
    static char retbuf[30];
    char *p = &retbuf[sizeof(retbuf) - 1];
    int i = 0;

    if (comma == '\0') {
        struct lconv *lcp = localeconv();
        if (lcp != NULL) {
            if (lcp->thousands_sep != NULL &&
                *lcp->thousands_sep != '\0')
                comma = *lcp->thousands_sep;
            else	comma = ',';
        }
    }

    *p = '\0';

    do {
        if (i % 3 == 0 && i != 0)
            *--p = (char)comma;
        *--p = '0' + number % 10;
        number /= 10;
        i++;
    } while (number != 0);

    return p;
}

} //namespace Divide::Util
