#ifndef RLL3_LISTS_H
#define RLL3_LISTS_H

#include <QList>

// Rugby League Live 3's lookup lists for the Database Editor (from Wouldy's RLL3 Database Editor):
// the stored number (or logo name) and the name to show, in the order to show them.
namespace Rll3Lists {

struct Choice {
    const char* name;
    int value;
};
struct TextChoice {
    const char* name;
    const char* value;
};

QList<Choice> countries();     // Player country of birth / representative country (u8).
QList<Choice> supporters();    // Team supporters (u8).
QList<TextChoice> emblems();   // Team logo names ("nrl_broncos").
QList<Choice> manufacturers(); // Jersey manufacturer (u8 in the uniform).
QList<Choice> licensedJerseys(int manufacturer);   // That manufacturer's licensed jerseys (u16 in the uniform).

} // namespace Rll3Lists

#endif // RLL3_LISTS_H
