#ifndef RLL4_LISTS_H
#define RLL4_LISTS_H

#include <QList>
#include <QStringList>

// Rugby League Live 4's lookup lists for the Database Editor (from Wouldy's RLL4 Database Editor).
// The string lists are by stored number (stadiums: the number - 5, like Rugby League 26).
namespace Rll4Lists {

struct Choice {
    const char* name;   // UTF-8
    int value;
};
struct TextChoice {
    const char* name;   // UTF-8
    const char* value;
};

QStringList countries();        // Player country of birth.
QStringList repCountries();     // Player representative country.
QStringList stadiums();         // Home grounds (stored number - 5).
QStringList commentaryTeams();  // Team commentary name.
QStringList supporters();       // Team supporters.
QList<TextChoice> logos();      // Team logo names ("nrl_broncos").
QList<Choice> manufacturers();  // Jersey manufacturer (the uniform's "Uniform" byte).
QList<Choice> licensedJerseys(int manufacturer);   // That manufacturer's jerseys (the uniform's "Top Pattern" byte).

} // namespace Rll4Lists

#endif // RLL4_LISTS_H
