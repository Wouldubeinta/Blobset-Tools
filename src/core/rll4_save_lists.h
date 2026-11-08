#ifndef RLL4_SAVE_LISTS_H
#define RLL4_SAVE_LISTS_H

#include <QList>
#include <QStringList>

// Rugby League Live 4 Save Editor drop down lists (from Wouldy's RLL4 Save Editor): by stored number unless said.
namespace Rll4SaveLists {

struct Choice {
    const char* name;   // UTF-8
    int value;
};

const QStringList& countriesOfBirth();
const QStringList& repCountries();
const QStringList& stadiums();            // The first 50 entries of a team's stadium list (see the manager).
const QStringList& commentarySurnames();  // Player commentary surname / nickname (5551).
const QStringList& commentaryTeams();
const QStringList& supporters();
const QStringList& logoNames();           // Team logo (262; 261 = custom).
const QStringList& logoFiles();           // The same logos' files ("nrl_broncos.pc.png").
const QStringList& headgear();
const QStringList& customLogos();         // The custom logo drop down's built in entries (None, Big Ant, sponsors).
const QList<Choice>& faceMeshes();        // Face mesh (name, stored id), in the editor's order.

} // namespace Rll4SaveLists

#endif // RLL4_SAVE_LISTS_H
