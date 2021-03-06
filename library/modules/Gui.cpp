/*
https://github.com/peterix/dfhack
Copyright (c) 2009-2011 Petr Mrázek (peterix@gmail.com)

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/


#include "Internal.h"

#include <string>
#include <vector>
#include <map>
using namespace std;

#include "modules/Gui.h"
#include "MemAccess.h"
#include "VersionInfo.h"
#include "Types.h"
#include "Error.h"
#include "ModuleFactory.h"
#include "Core.h"
#include "PluginManager.h"
#include "MiscUtils.h"
using namespace DFHack;

#include "modules/Job.h"

#include "DataDefs.h"
#include "df/world.h"
#include "df/global_objects.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_dungeonmodest.h"
#include "df/viewscreen_dungeon_monsterstatusst.h"
#include "df/viewscreen_joblistst.h"
#include "df/viewscreen_unitlistst.h"
#include "df/viewscreen_itemst.h"
#include "df/viewscreen_layerst.h"
#include "df/viewscreen_layer_workshop_profilest.h"
#include "df/viewscreen_layer_noblelistst.h"
#include "df/viewscreen_layer_overall_healthst.h"
#include "df/viewscreen_layer_assigntradest.h"
#include "df/viewscreen_layer_militaryst.h"
#include "df/viewscreen_petst.h"
#include "df/viewscreen_tradegoodsst.h"
#include "df/viewscreen_storesst.h"
#include "df/ui_unit_view_mode.h"
#include "df/ui_sidebar_menus.h"
#include "df/ui_look_list.h"
#include "df/job.h"
#include "df/ui_build_selector.h"
#include "df/building_workshopst.h"
#include "df/building_furnacest.h"
#include "df/building_trapst.h"
#include "df/building_civzonest.h"
#include "df/general_ref.h"
#include "df/unit_inventory_item.h"
#include "df/report.h"
#include "df/popup_message.h"
#include "df/interfacest.h"
#include "df/graphic.h"
#include "df/layer_object_listst.h"
#include "df/assign_trade_status.h"

using namespace df::enums;
using df::global::gview;
using df::global::init;
using df::global::gps;
using df::global::ui;
using df::global::world;

static df::layer_object_listst *getLayerList(df::viewscreen_layerst *layer, int idx)
{
    return virtual_cast<df::layer_object_listst>(vector_get(layer->layer_objects,idx));
}

static std::string getNameChunk(virtual_identity *id, int start, int end)
{
    if (!id)
        return "UNKNOWN";
    const char *name = id->getName();
    int len = strlen(name);
    if (len > start + end)
        return std::string(name+start, len-start-end);
    else
        return name;
}

/*
 * Classifying focus context by means of a string path.
 */

typedef void (*getFocusStringHandler)(std::string &str, df::viewscreen *screen);
static std::map<virtual_identity*, getFocusStringHandler> getFocusStringHandlers;

#define VIEWSCREEN(name) df::viewscreen_##name##st
#define DEFINE_GET_FOCUS_STRING_HANDLER(screen_type) \
    static void getFocusString_##screen_type(std::string &focus, VIEWSCREEN(screen_type) *screen);\
    DFHACK_STATIC_ADD_TO_MAP(\
        &getFocusStringHandlers, &VIEWSCREEN(screen_type)::_identity, \
        (getFocusStringHandler)getFocusString_##screen_type \
    ); \
    static void getFocusString_##screen_type(std::string &focus, VIEWSCREEN(screen_type) *screen)

DEFINE_GET_FOCUS_STRING_HANDLER(dwarfmode)
{
    using namespace df::enums::ui_sidebar_mode;

    using df::global::ui_workshop_in_add;
    using df::global::ui_build_selector;
    using df::global::ui_selected_unit;
    using df::global::ui_look_list;
    using df::global::ui_look_cursor;
    using df::global::ui_building_item_cursor;
    using df::global::ui_building_assign_type;
    using df::global::ui_building_assign_is_marked;
    using df::global::ui_building_assign_units;
    using df::global::ui_building_assign_items;
    using df::global::ui_building_in_assign;

    focus += "/" + enum_item_key(ui->main.mode);

    switch (ui->main.mode)
    {
    case QueryBuilding:
        if (df::building *selected = world->selected_building)
        {
            if (!selected->jobs.empty() &&
                selected->jobs[0]->job_type == job_type::DestroyBuilding)
            {
                focus += "/Destroying";
                break;
            }

            focus += "/Some";

            virtual_identity *id = virtual_identity::get(selected);

            bool jobs = false;

            if (id == &df::building_workshopst::_identity ||
                id == &df::building_furnacest::_identity)
            {
                focus += "/Workshop";
                jobs = true;
            }
            else if (id == &df::building_trapst::_identity)
            {
                auto trap = (df::building_trapst*)selected;
                if (trap->trap_type == trap_type::Lever) {
                    focus += "/Lever";
                    jobs = true;
                }
            }
            else if (ui_building_in_assign && *ui_building_in_assign &&
                     ui_building_assign_type && ui_building_assign_units &&
                     ui_building_assign_type->size() == ui_building_assign_units->size())
            {
                focus += "/Assign";
                if (ui_building_item_cursor)
                {
                    auto unit = vector_get(*ui_building_assign_units, *ui_building_item_cursor);
                    focus += unit ? "/Unit" : "/None";
                }
            }

            if (jobs)
            {
                if (ui_workshop_in_add && *ui_workshop_in_add)
                    focus += "/AddJob";
                else if (!selected->jobs.empty())
                    focus += "/Job";
                else
                    focus += "/Empty";
            }
        }
        else
            focus += "/None";
        break;

    case Build:
        if (ui_build_selector)
        {
            // Not selecting, or no choices?
            if (ui_build_selector->building_type < 0)
                focus += "/Type";
            else if (ui_build_selector->stage != 2)
                focus += "/Position";
            else
            {
                focus += "/Material";
                if (ui_build_selector->is_grouped)
                    focus += "/Groups";
                else
                    focus += "/Items";
            }
        }
        break;

    case ViewUnits:
        if (ui_selected_unit)
        {
            if (auto unit = vector_get(world->units.active, *ui_selected_unit))
            {
                focus += "/Some";

                using df::global::ui_unit_view_mode;

                if (ui_unit_view_mode)
                    focus += "/" + enum_item_key(ui_unit_view_mode->value);
            }
            else
                focus += "/None";
        }
        break;

    case LookAround:
        if (ui_look_list && ui_look_cursor)
        {
            auto item = vector_get(ui_look_list->items, *ui_look_cursor);
            if (item)
                focus += "/" + enum_item_key(item->type);
            else
                focus += "/None";
        }
        break;

    case BuildingItems:
        if (VIRTUAL_CAST_VAR(selected, df::building_actual, world->selected_building))
        {
            if (selected->contained_items.empty())
                focus += "/Some/Empty";
            else
                focus += "/Some/Item";
        }
        else
            focus += "/None";
        break;

    case ZonesPenInfo:
        if (ui_building_assign_type && ui_building_assign_units &&
            ui_building_assign_is_marked && ui_building_assign_items &&
            ui_building_assign_type->size() == ui_building_assign_units->size())
        {
            focus += "/Assign";
            if (ui_building_item_cursor)
            {
                if (vector_get(*ui_building_assign_units, *ui_building_item_cursor))
                    focus += "/Unit";
                else if (vector_get(*ui_building_assign_items, *ui_building_item_cursor))
                    focus += "/Vermin";
                else
                    focus += "/None";
            }
        }
        break;

    case Burrows:
        if (ui->burrows.in_add_units_mode)
            focus += "/AddUnits";
        else if (ui->burrows.in_edit_name_mode)
            focus += "/EditName";
        else if (ui->burrows.in_define_mode)
            focus += "/Define";
        else
            focus += "/List";
        break;

    default:
        break;
    }
}

DEFINE_GET_FOCUS_STRING_HANDLER(unitlist)
{
    focus += "/" + enum_item_key(screen->page);
}

DEFINE_GET_FOCUS_STRING_HANDLER(layer_military)
{
    auto list1 = getLayerList(screen, 0);
    auto list2 = getLayerList(screen, 1);
    auto list3 = getLayerList(screen, 2);
    if (!list1 || !list2 || !list3) return;

    focus += "/" + enum_item_key(screen->page);

    int cur_list;
    if (list1->bright) cur_list = 0;
    else if (list2->bright) cur_list = 1;
    else if (list3->bright) cur_list = 2;
    else return;

    switch (screen->page)
    {
    case df::viewscreen_layer_militaryst::Positions:
        {
            static const char *lists[] = { "/Squads", "/Positions", "/Candidates" };
            focus += lists[cur_list];
            break;
        }

    default:
        break;
    }
}

DEFINE_GET_FOCUS_STRING_HANDLER(layer_workshop_profile)
{
    auto list1 = getLayerList(screen, 0);
    if (!list1) return;

    if (vector_get(screen->workers, list1->cursor))
        focus += "/Unit";
    else
        focus += "/None";
}

DEFINE_GET_FOCUS_STRING_HANDLER(layer_noblelist)
{
    auto list1 = getLayerList(screen, 0);
    auto list2 = getLayerList(screen, 1);
    if (!list1 || !list2) return;

    focus += "/" + enum_item_key(screen->mode);
}

DEFINE_GET_FOCUS_STRING_HANDLER(pet)
{
    focus += "/" + enum_item_key(screen->mode);

    switch (screen->mode)
    {
    case df::viewscreen_petst::List:
        focus += vector_get(screen->is_vermin, screen->cursor) ? "/Vermin" : "/Unit";
        break;

    case df::viewscreen_petst::SelectTrainer:
        if (vector_get(screen->trainer_unit, screen->trainer_cursor))
            focus += "/Unit";
        break;

    default:
        break;
    }
}

DEFINE_GET_FOCUS_STRING_HANDLER(layer_overall_health)
{
    auto list1 = getLayerList(screen, 0);
    if (!list1) return;

    focus += "/Units";
}

DEFINE_GET_FOCUS_STRING_HANDLER(tradegoods)
{
    if (!screen->has_traders || screen->is_unloading)
        focus += "/NoTraders";
    else if (screen->in_edit_count)
        focus += "/EditCount";
    else
        focus += (screen->in_right_pane ? "/Items/Broker" : "/Items/Trader");
}

DEFINE_GET_FOCUS_STRING_HANDLER(layer_assigntrade)
{
    auto list1 = getLayerList(screen, 0);
    auto list2 = getLayerList(screen, 1);
    if (!list1 || !list2) return;

    int list_idx = vector_get(screen->visible_lists, list1->cursor, (int16_t)-1);
    unsigned num_lists = sizeof(screen->lists)/sizeof(screen->lists[0]);
    if (unsigned(list_idx) >= num_lists)
        return;

    if (list1->bright)
        focus += "/Groups";
    else
        focus += "/Items";
}

DEFINE_GET_FOCUS_STRING_HANDLER(stores)
{
    if (!screen->in_right_list)
        focus += "/Categories";
    else if (screen->in_group_mode)
        focus += "/Groups";
    else
        focus += "/Items";
}


std::string Gui::getFocusString(df::viewscreen *top)
{
    if (!top)
        return "";

    if (virtual_identity *id = virtual_identity::get(top))
    {
        std::string name = getNameChunk(id, 11, 2);

        auto handler = map_find(getFocusStringHandlers, id);
        if (handler)
            handler(name, top);

        return name;
    }
    else
    {
        Core &core = Core::getInstance();
        std::string name = core.p->readClassName(*(void**)top);
        return name.substr(11, name.size()-11-2);
    }
}

// Predefined common guard functions

bool Gui::default_hotkey(df::viewscreen *top)
{
    // Default hotkey guard function
    for (;top ;top = top->parent)
    {
        if (strict_virtual_cast<df::viewscreen_dwarfmodest>(top))
            return true;
        if (strict_virtual_cast<df::viewscreen_dungeonmodest>(top))
            return true;
    }
    return false;
}

bool Gui::dwarfmode_hotkey(df::viewscreen *top)
{
    // Require the main dwarf mode screen
    return !!strict_virtual_cast<df::viewscreen_dwarfmodest>(top);
}

bool Gui::unitjobs_hotkey(df::viewscreen *top)
{
    // Require the unit or jobs list
    return !!strict_virtual_cast<df::viewscreen_joblistst>(top) ||
           !!strict_virtual_cast<df::viewscreen_unitlistst>(top);
}

bool Gui::item_details_hotkey(df::viewscreen *top)
{
    // Require the main dwarf mode screen
    return !!strict_virtual_cast<df::viewscreen_itemst>(top);
}

bool Gui::cursor_hotkey(df::viewscreen *top)
{
    if (!dwarfmode_hotkey(top))
        return false;

    // Also require the cursor.
    if (!df::global::cursor || df::global::cursor->x == -30000)
        return false;

    return true;
}

bool Gui::workshop_job_hotkey(df::viewscreen *top)
{
    using namespace ui_sidebar_mode;
    using df::global::ui;
    using df::global::world;
    using df::global::ui_workshop_in_add;
    using df::global::ui_workshop_job_cursor;

    if (!dwarfmode_hotkey(top))
        return false;

    switch (ui->main.mode) {
    case QueryBuilding:
        {
            if (!ui_workshop_job_cursor) // allow missing
                return false;

            df::building *selected = world->selected_building;
            if (!virtual_cast<df::building_workshopst>(selected) &&
                !virtual_cast<df::building_furnacest>(selected))
                return false;

            // No jobs?
            if (selected->jobs.empty() ||
                selected->jobs[0]->job_type == job_type::DestroyBuilding)
                return false;

            // Add job gui activated?
            if (ui_workshop_in_add && *ui_workshop_in_add)
                return false;

            return true;
        };
    default:
        return false;
    }
}

bool Gui::build_selector_hotkey(df::viewscreen *top)
{
    using namespace ui_sidebar_mode;
    using df::global::ui;
    using df::global::ui_build_selector;

    if (!dwarfmode_hotkey(top))
        return false;

    switch (ui->main.mode) {
    case Build:
        {
            if (!ui_build_selector) // allow missing
                return false;

            // Not selecting, or no choices?
            if (ui_build_selector->building_type < 0 ||
                ui_build_selector->stage != 2 ||
                ui_build_selector->choices.empty())
                return false;

            return true;
        };
    default:
        return false;
    }
}

bool Gui::view_unit_hotkey(df::viewscreen *top)
{
    using df::global::ui;
    using df::global::world;
    using df::global::ui_selected_unit;

    if (!dwarfmode_hotkey(top))
        return false;
    if (ui->main.mode != ui_sidebar_mode::ViewUnits)
        return false;
    if (!ui_selected_unit) // allow missing
        return false;

    return vector_get(world->units.active, *ui_selected_unit) != NULL;
}

bool Gui::unit_inventory_hotkey(df::viewscreen *top)
{
    using df::global::ui_unit_view_mode;

    if (!view_unit_hotkey(top))
        return false;
    if (!ui_unit_view_mode)
        return false;

    return ui_unit_view_mode->value == df::ui_unit_view_mode::Inventory;
}

df::job *Gui::getSelectedWorkshopJob(color_ostream &out, bool quiet)
{
    using df::global::world;
    using df::global::ui_workshop_job_cursor;

    if (!workshop_job_hotkey(Core::getTopViewscreen())) {
        if (!quiet)
            out.printerr("Not in a workshop, or no job is highlighted.\n");
        return NULL;
    }

    df::building *selected = world->selected_building;
    int idx = *ui_workshop_job_cursor;

    if (size_t(idx) >= selected->jobs.size())
    {
        out.printerr("Invalid job cursor index: %d\n", idx);
        return NULL;
    }

    return selected->jobs[idx];
}

bool Gui::any_job_hotkey(df::viewscreen *top)
{
    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_joblistst, top))
        return vector_get(screen->jobs, screen->cursor_pos) != NULL;

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_unitlistst, top))
        return vector_get(screen->jobs[screen->page], screen->cursor_pos[screen->page]) != NULL;

    return workshop_job_hotkey(top);
}

df::job *Gui::getSelectedJob(color_ostream &out, bool quiet)
{
    df::viewscreen *top = Core::getTopViewscreen();

    if (VIRTUAL_CAST_VAR(joblist, df::viewscreen_joblistst, top))
    {
        df::job *job = vector_get(joblist->jobs, joblist->cursor_pos);

        if (!job && !quiet)
            out.printerr("Selected unit has no job\n");

        return job;
    }
    else if (VIRTUAL_CAST_VAR(unitlist, df::viewscreen_unitlistst, top))
    {
        int page = unitlist->page;
        df::job *job = vector_get(unitlist->jobs[page], unitlist->cursor_pos[page]);

        if (!job && !quiet)
            out.printerr("Selected unit has no job\n");

        return job;
    }
    else
        return getSelectedWorkshopJob(out, quiet);
}

static df::unit *getAnyUnit(df::viewscreen *top)
{
    using namespace ui_sidebar_mode;
    using df::global::ui;
    using df::global::world;
    using df::global::ui_look_cursor;
    using df::global::ui_look_list;
    using df::global::ui_selected_unit;

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_joblistst, top))
    {
        if (auto unit = vector_get(screen->units, screen->cursor_pos))
            return unit;
        if (auto job = vector_get(screen->jobs, screen->cursor_pos))
            return Job::getWorker(job);
        return NULL;
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_unitlistst, top))
        return vector_get(screen->units[screen->page], screen->cursor_pos[screen->page]);

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_dungeon_monsterstatusst, top))
        return screen->unit;

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_itemst, top))
    {
        df::general_ref *ref = vector_get(screen->entry_ref, screen->cursor_pos);
        return ref ? ref->getUnit() : NULL;
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_layer_workshop_profilest, top))
    {
        if (auto list1 = getLayerList(screen, 0))
            return vector_get(screen->workers, list1->cursor);
        return NULL;
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_layer_noblelistst, top))
    {
        switch (screen->mode)
        {
        case df::viewscreen_layer_noblelistst::List:
            if (auto list1 = getLayerList(screen, 0))
            {
                if (auto info = vector_get(screen->info, list1->cursor))
                    return info->unit;
            }
            return NULL;

        case df::viewscreen_layer_noblelistst::Appoint:
            if (auto list2 = getLayerList(screen, 1))
            {
                if (auto info = vector_get(screen->candidates, list2->cursor))
                    return info->unit;
            }
            return NULL;

        default:
            return NULL;
        }
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_petst, top))
    {
        switch (screen->mode)
        {
        case df::viewscreen_petst::List:
            if (!vector_get(screen->is_vermin, screen->cursor))
                return (df::unit*)vector_get(screen->animal, screen->cursor);
            return NULL;

        case df::viewscreen_petst::SelectTrainer:
            return vector_get(screen->trainer_unit, screen->trainer_cursor);

        default:
            return NULL;
        }
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_layer_overall_healthst, top))
    {
        if (auto list1 = getLayerList(screen, 0))
            return vector_get(screen->unit, list1->cursor);
        return NULL;
    }

    if (!Gui::dwarfmode_hotkey(top))
        return NULL;

    switch (ui->main.mode) {
    case ViewUnits:
    {
        if (!ui_selected_unit)
            return NULL;

        return vector_get(world->units.active, *ui_selected_unit);
    }
    case LookAround:
    {
        if (!ui_look_list || !ui_look_cursor)
            return NULL;

        auto item = vector_get(ui_look_list->items, *ui_look_cursor);
        if (item && item->type == df::ui_look_list::T_items::Unit)
            return item->unit;
        else
            return NULL;
    }
    default:
        return NULL;
    }
}

bool Gui::any_unit_hotkey(df::viewscreen *top)
{
    return getAnyUnit(top) != NULL;
}

df::unit *Gui::getSelectedUnit(color_ostream &out, bool quiet)
{
    df::unit *unit = getAnyUnit(Core::getTopViewscreen());

    if (!unit && !quiet)
        out.printerr("No unit is selected in the UI.\n");

    return unit;
}

static df::item *getAnyItem(df::viewscreen *top)
{
    using namespace ui_sidebar_mode;
    using df::global::ui;
    using df::global::world;
    using df::global::ui_look_cursor;
    using df::global::ui_look_list;
    using df::global::ui_unit_view_mode;
    using df::global::ui_building_item_cursor;
    using df::global::ui_sidebar_menus;

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_itemst, top))
    {
        df::general_ref *ref = vector_get(screen->entry_ref, screen->cursor_pos);
        return ref ? ref->getItem() : NULL;
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_layer_assigntradest, top))
    {
        auto list1 = getLayerList(screen, 0);
        auto list2 = getLayerList(screen, 1);
        if (!list1 || !list2 || !list2->bright)
            return NULL;

        int list_idx = vector_get(screen->visible_lists, list1->cursor, (int16_t)-1);
        unsigned num_lists = sizeof(screen->lists)/sizeof(std::vector<int32_t>);
        if (unsigned(list_idx) >= num_lists)
            return NULL;

        int idx = vector_get(screen->lists[list_idx], list2->cursor, -1);
        if (auto info = vector_get(screen->info, idx))
            return info->item;

        return NULL;
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_tradegoodsst, top))
    {
        if (screen->in_right_pane)
            return vector_get(screen->broker_items, screen->broker_cursor);
        else
            return vector_get(screen->trader_items, screen->trader_cursor);
    }

    if (VIRTUAL_CAST_VAR(screen, df::viewscreen_storesst, top))
    {
        if (screen->in_right_list && !screen->in_group_mode)
            return vector_get(screen->items, screen->item_cursor);

        return NULL;
    }

    if (!Gui::dwarfmode_hotkey(top))
        return NULL;

    switch (ui->main.mode) {
    case ViewUnits:
    {
        if (!ui_unit_view_mode || !ui_look_cursor || !ui_sidebar_menus)
            return NULL;

        if (ui_unit_view_mode->value != df::ui_unit_view_mode::Inventory)
            return NULL;

        auto inv_item = vector_get(ui_sidebar_menus->unit.inv_items, *ui_look_cursor);
        return inv_item ? inv_item->item : NULL;
    }
    case LookAround:
    {
        if (!ui_look_list || !ui_look_cursor)
            return NULL;

        auto item = vector_get(ui_look_list->items, *ui_look_cursor);
        if (item && item->type == df::ui_look_list::T_items::Item)
            return item->item;
        else
            return NULL;
    }
    case BuildingItems:
    {
        if (!ui_building_item_cursor)
            return NULL;

        VIRTUAL_CAST_VAR(selected, df::building_actual, world->selected_building);
        if (!selected)
            return NULL;

        auto inv_item = vector_get(selected->contained_items, *ui_building_item_cursor);
        return inv_item ? inv_item->item : NULL;
    }
    default:
        return NULL;
    }
}

bool Gui::any_item_hotkey(df::viewscreen *top)
{
    return getAnyItem(top) != NULL;
}

df::item *Gui::getSelectedItem(color_ostream &out, bool quiet)
{
    df::item *item = getAnyItem(Core::getTopViewscreen());

    if (!item && !quiet)
        out.printerr("No item is selected in the UI.\n");

    return item;
}

//

void Gui::showAnnouncement(std::string message, int color, bool bright)
{
    using df::global::world;
    using df::global::cur_year;
    using df::global::cur_year_tick;

    int year = 0, year_time = 0;

    if (cur_year && cur_year_tick)
    {
        year = *cur_year;
        year_time = *cur_year_tick;
    }
    else if (!world->status.reports.empty())
    {
        // Fallback: copy from the last report
        df::report *last = world->status.reports.back();
        year = last->year;
        year_time = last->time;
    }

    bool continued = false;

    while (!message.empty())
    {
        df::report *new_rep = new df::report();

        new_rep->color = color;
        new_rep->bright = bright;
        new_rep->year = year;
        new_rep->time = year_time;

        new_rep->flags.bits.continuation = continued;
        new_rep->flags.bits.announcement = true;

        int size = std::min(message.size(), 73U);
        new_rep->text = message.substr(0, size);
        message = message.substr(size);

        continued = true;

        // Add the object to the lists
        new_rep->id = world->status.next_report_id++;

        world->status.reports.push_back(new_rep);
        world->status.announcements.push_back(new_rep);
        world->status.display_timer = 2000;
    }

}

void Gui::showPopupAnnouncement(std::string message, int color, bool bright)
{
    using df::global::world;

    df::popup_message *popup = new df::popup_message();
    popup->text = message;
    popup->color = color;
    popup->bright = bright;
    world->status.popups.push_back(popup);
}

df::viewscreen * Gui::GetCurrentScreen()
{
    df::viewscreen * ws = &gview->view;
    while(ws)
    {
        if(ws->child)
            ws = ws->child;
        else
            return ws;
    }
    return 0;
}

bool Gui::getViewCoords (int32_t &x, int32_t &y, int32_t &z)
{
    x = *df::global::window_x;
    y = *df::global::window_y;
    z = *df::global::window_z;
    return true;
}

bool Gui::setViewCoords (const int32_t x, const int32_t y, const int32_t z)
{
    (*df::global::window_x) = x;
    (*df::global::window_y) = y;
    (*df::global::window_z) = z;
    return true;
}

bool Gui::getCursorCoords (int32_t &x, int32_t &y, int32_t &z)
{
    x = df::global::cursor->x;
    y = df::global::cursor->y;
    z = df::global::cursor->z;
    return (x == -30000) ? false : true;
}

//FIXME: confine writing of coords to map bounds?
bool Gui::setCursorCoords (const int32_t x, const int32_t y, const int32_t z)
{
    df::global::cursor->x = x;
    df::global::cursor->y = y;
    df::global::cursor->z = z;
    return true;
}

bool Gui::getDesignationCoords (int32_t &x, int32_t &y, int32_t &z)
{
    x = df::global::selection_rect->start_x;
    y = df::global::selection_rect->start_y;
    z = df::global::selection_rect->start_z;
    return (x == -30000) ? false : true;
}

bool Gui::setDesignationCoords (const int32_t x, const int32_t y, const int32_t z)
{
    df::global::selection_rect->start_x = x;
    df::global::selection_rect->start_y = y;
    df::global::selection_rect->start_z = z;
    return true;
}

bool Gui::getMousePos (int32_t & x, int32_t & y)
{
    x = gps->mouse_x;
    y = gps->mouse_y;
    return (x == -1) ? false : true;
}

bool Gui::getWindowSize (int32_t &width, int32_t &height)
{
    width = gps->dimx;
    height = gps->dimy;
    return true;
}

bool Gui::getMenuWidth(uint8_t &menu_width, uint8_t &area_map_width)
{
    menu_width = *df::global::ui_menu_width;
    area_map_width = *df::global::ui_area_map_width;
    return true;
}

bool Gui::setMenuWidth(const uint8_t menu_width, const uint8_t area_map_width)
{
    *df::global::ui_menu_width = menu_width;
    *df::global::ui_area_map_width = area_map_width;
    return true;
}
