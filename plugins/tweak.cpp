// A container for random minor tweaks that don't fit anywhere else,
// in order to avoid creating lots of plugins and polluting the namespace.

#include "Core.h"
#include "Console.h"
#include "Export.h"
#include "PluginManager.h"

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Units.h"
#include "modules/Items.h"

#include "MiscUtils.h"

#include "DataDefs.h"
#include <VTableInterpose.h>
#include "df/ui.h"
#include "df/world.h"
#include "df/squad.h"
#include "df/unit.h"
#include "df/unit_soul.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/historical_figure_info.h"
#include "df/assumed_identity.h"
#include "df/language_name.h"
#include "df/death_info.h"
#include "df/criminal_case.h"
#include "df/unit_inventory_item.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_layer_unit_actionst.h"
#include "df/squad_order_trainst.h"
#include "df/ui_build_selector.h"
#include "df/building_trapst.h"
#include "df/item_actual.h"
#include "df/item_liquipowder.h"
#include "df/item_barst.h"
#include "df/item_threadst.h"
#include "df/item_clothst.h"
#include "df/contaminant.h"
#include "df/layer_object.h"
#include "df/reaction.h"
#include "df/reaction_reagent_itemst.h"
#include "df/reaction_reagent_flags.h"
#include "df/viewscreen_layer_assigntradest.h"
#include "df/viewscreen_tradegoodsst.h"
#include "df/viewscreen_layer_militaryst.h"

#include <stdlib.h>

using std::vector;
using std::string;
using std::endl;
using namespace DFHack;
using namespace df::enums;

using df::global::ui;
using df::global::world;
using df::global::ui_build_selector;
using df::global::ui_menu_width;
using df::global::ui_area_map_width;

using namespace DFHack::Gui;
using Screen::Pen;

static command_result tweak(color_ostream &out, vector <string> & parameters);

DFHACK_PLUGIN("tweak");

DFhackCExport command_result plugin_init (color_ostream &out, std::vector <PluginCommand> &commands)
{
    commands.push_back(PluginCommand(
        "tweak", "Various tweaks for minor bugs.", tweak, false,
        "  tweak clear-missing\n"
        "    Remove the missing status from the selected unit.\n"
        "  tweak clear-ghostly\n"
        "    Remove the ghostly status from the selected unit.\n"
        "    Intended to fix the case where you can't engrave memorials for ghosts.\n"
        "    Note that this is very dirty and possibly dangerous!\n"
        "    Most probably does not have the positive effect of a proper burial.\n"
        "  tweak fixmigrant\n"
        "    Remove the resident/merchant flag from the selected unit.\n"
        "    Intended to fix bugged migrants/traders who stay at the\n"
        "    map edge and don't enter your fort. Only works for\n"
        "    dwarves (or generally the player's race in modded games).\n"
        "  tweak makeown\n"
        "    Force selected unit to become a member of your fort.\n"
        "    Can be abused to grab caravan merchants and escorts, even if\n"
        "    they don't belong to the player's race. Foreign sentients\n"
        "    (humans, elves) can be put to work, but you can't assign rooms\n"
        "    to them and they don't show up in DwarfTherapist because the\n"
        "    game treats them like pets.\n"
        "  tweak stable-cursor [disable]\n"
        "    Keeps exact position of dwarfmode cursor during exits to main menu.\n"
        "    E.g. allows switching between t/q/k/d without losing position.\n"
        "  tweak patrol-duty [disable]\n"
        "    Causes 'Train' orders to no longer be considered 'patrol duty' so\n"
        "    soldiers will stop getting unhappy thoughts. Does NOT fix the problem\n"
        "    when soldiers go off-duty (i.e. civilian).\n"
        "  tweak readable-build-plate [disable]\n"
        "    Fixes rendering of creature weight limits in pressure plate build menu.\n"
        "  tweak stable-temp [disable]\n"
        "    Fixes performance bug 6012 by squashing jitter in temperature updates.\n"
        "  tweak fast-heat <max-ticks>\n"
        "    Further improves temperature updates by ensuring that 1 degree of\n"
        "    item temperature is crossed in no more than specified number of frames\n"
        "    when updating from the environment temperature. Use 0 to disable.\n"
        "  tweak fix-dimensions [disable]\n"
        "    Fixes subtracting small amount of thread/cloth/liquid from a stack\n"
        "    by splitting the stack and subtracting from the remaining single item.\n"
        "  tweak advmode-contained [disable]\n"
        "    Fixes custom reactions with container inputs in advmode. The issue is\n"
        "    that the screen tries to force you to select the contents separately\n"
        "    from the container. This forcefully skips child reagents.\n"
        "  tweak fast-trade [disable]\n"
        "    Makes Shift-Enter in the Move Goods to Depot and Trade screens select\n"
        "    the current item (fully, in case of a stack), and scroll down one line.\n"
        "  tweak military-stable-assign [disable]\n"
        "    Preserve list order and cursor position when assigning to squad,\n"
        "    i.e. stop the rightmost list of the Positions page of the military\n"
        "    screen from constantly jumping to the top.\n"
        "  tweak military-color-assigned [disable]\n"
        "    Color squad candidates already assigned to other squads in brown/green\n"
        "    to make them stand out more in the list.\n"
    ));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown (color_ostream &out)
{
    return CR_OK;
}

// to be called by tweak-fixmigrant
// units forced into the fort by removing the flags do not own their clothes
// which has the result that they drop all their clothes and become unhappy because they are naked
// so we need to make them own their clothes and add them to their uniform
command_result fix_clothing_ownership(color_ostream &out, df::unit* unit)
{
    // first, find one owned item to initialize the vtable
    bool vt_initialized = false;
    size_t numItems = world->items.all.size();
    for(size_t i=0; i< numItems; i++)
    {
        df::item * item = world->items.all[i];
        if(Items::getOwner(item))
        {
            vt_initialized = true;
            break;
        }
    }
    if(!vt_initialized)
    {
        out << "fix_clothing_ownership: could not initialize vtable!" << endl;
        return CR_FAILURE;
    }

    int fixcount = 0;
    for(size_t j=0; j<unit->inventory.size(); j++)
    {
        df::unit_inventory_item* inv_item = unit->inventory[j];
        df::item* item = inv_item->item;
        // unforbid items (for the case of kidnapping caravan escorts who have their stuff forbidden by default)
        inv_item->item->flags.bits.forbid = 0;
        if(inv_item->mode == df::unit_inventory_item::T_mode::Worn)
        {
            // ignore armor?
            // it could be leather boots, for example, in which case it would not be nice to forbid ownership
            //if(item->getEffectiveArmorLevel() != 0)
            //    continue;

            if(!Items::getOwner(item))
            {
                if(Items::setOwner(item, unit))
                {
                    // add to uniform, so they know they should wear their clothes
                    insert_into_vector(unit->military.uniforms[0], item->id);
                    fixcount++;
                }
                else
                    out << "could not change ownership for item!" << endl;
            }
        }
    }
    // clear uniform_drop (without this they would drop their clothes and pick them up some time later)
    unit->military.uniform_drop.clear();
    out << "ownership for " << fixcount << " clothes fixed" << endl;
    return CR_OK;
}

/*
 * Save or restore cursor position on change to/from main dwarfmode menu.
 */

static df::coord last_view, last_cursor;

struct stable_cursor_hook : df::viewscreen_dwarfmodest
{
    typedef df::viewscreen_dwarfmodest interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *input))
    {
        bool was_default = (ui->main.mode == df::ui_sidebar_mode::Default);
        df::coord view = Gui::getViewportPos();
        df::coord cursor = Gui::getCursorPos();

        INTERPOSE_NEXT(feed)(input);

        bool is_default = (ui->main.mode == df::ui_sidebar_mode::Default);
        df::coord cur_cursor = Gui::getCursorPos();

        if (is_default && !was_default)
        {
            last_view = view; last_cursor = cursor;
        }
        else if (!is_default && was_default &&
                 Gui::getViewportPos() == last_view &&
                 last_cursor.isValid() && cur_cursor.isValid())
        {
            Gui::setCursorCoords(last_cursor.x, last_cursor.y, last_cursor.z);

            // Force update of ui state
            set<df::interface_key> tmp;
            tmp.insert(interface_key::CURSOR_DOWN_Z);
            INTERPOSE_NEXT(feed)(&tmp);
            tmp.clear();
            tmp.insert(interface_key::CURSOR_UP_Z);
            INTERPOSE_NEXT(feed)(&tmp);
        }
        else if (cur_cursor.isValid())
        {
            last_cursor = df::coord();
        }
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(stable_cursor_hook, feed);

struct patrol_duty_hook : df::squad_order_trainst
{
    typedef df::squad_order_trainst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(bool, isPatrol, ())
    {
        return false;
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(patrol_duty_hook, isPatrol);

struct readable_build_plate_hook : df::viewscreen_dwarfmodest
{
    typedef df::viewscreen_dwarfmodest interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, render, ())
    {
        INTERPOSE_NEXT(render)();

        if (ui->main.mode == ui_sidebar_mode::Build &&
            ui_build_selector->stage == 1 &&
            ui_build_selector->building_type == building_type::Trap &&
            ui_build_selector->building_subtype == trap_type::PressurePlate &&
            ui_build_selector->plate_info.flags.bits.units)
        {
            auto dims = Gui::getDwarfmodeViewDims();
            int x = dims.menu_x1;

            Screen::Pen pen(' ',COLOR_WHITE);

            int minv = ui_build_selector->plate_info.unit_min;
            if ((minv % 1000) == 0)
                Screen::paintString(pen, x+11, 14, stl_sprintf("%3dK ", minv/1000));

            int maxv = ui_build_selector->plate_info.unit_max;
            if (maxv < 200000 && (maxv % 1000) == 0)
                Screen::paintString(pen, x+24, 14, stl_sprintf("%3dK ", maxv/1000));
        }
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(readable_build_plate_hook, render);

struct stable_temp_hook : df::item_actual {
    typedef df::item_actual interpose_base;

    DEFINE_VMETHOD_INTERPOSE(bool, adjustTemperature, (uint16_t temp, int32_t rate_mult))
    {
        if (temperature != temp)
        {
            // Bug 6012 is caused by fixed-point precision mismatch jitter
            // when an item is being pushed by two sources at N and N+1.
            // This check suppresses it altogether.
            if (temp == temperature+1 ||
                (temp == temperature-1 && temperature_fraction == 0))
                temp = temperature;
            // When SPEC_HEAT is NONE, the original function seems to not
            // change the temperature, yet return true, which is silly.
            else if (getSpecHeat() == 60001)
                temp = temperature;
        }

        return INTERPOSE_NEXT(adjustTemperature)(temp, rate_mult);
    }

    DEFINE_VMETHOD_INTERPOSE(bool, updateContaminants, ())
    {
        if (contaminants)
        {
            // Force 1-degree difference in contaminant temperature to 0
            for (size_t i = 0; i < contaminants->size(); i++)
            {
                auto obj = (*contaminants)[i];

                if (abs(obj->temperature - temperature) == 1)
                {
                    obj->temperature = temperature;
                    obj->temperature_fraction = temperature_fraction;
                }
            }
        }

        return INTERPOSE_NEXT(updateContaminants)();
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(stable_temp_hook, adjustTemperature);
IMPLEMENT_VMETHOD_INTERPOSE(stable_temp_hook, updateContaminants);

static int map_temp_mult = -1;
static int max_heat_ticks = 0;

struct fast_heat_hook : df::item_actual {
    typedef df::item_actual interpose_base;

    DEFINE_VMETHOD_INTERPOSE(
        bool, updateTempFromMap,
        (bool local, bool contained, bool adjust, int32_t rate_mult)
    ) {
        int cmult = map_temp_mult;
        map_temp_mult = rate_mult;

        bool rv = INTERPOSE_NEXT(updateTempFromMap)(local, contained, adjust, rate_mult);
        map_temp_mult = cmult;
        return rv;
    }

    DEFINE_VMETHOD_INTERPOSE(
        bool, updateTemperature,
        (uint16_t temp, bool local, bool contained, bool adjust, int32_t rate_mult)
    ) {
        // Some items take ages to cross the last degree, so speed them up
        if (map_temp_mult > 0 && temp != temperature && max_heat_ticks > 0)
        {
            int spec = getSpecHeat();
            if (spec != 60001)
                rate_mult = std::max(map_temp_mult, spec/max_heat_ticks/abs(temp - temperature));
        }

        return INTERPOSE_NEXT(updateTemperature)(temp, local, contained, adjust, rate_mult);
    }

    DEFINE_VMETHOD_INTERPOSE(bool, adjustTemperature, (uint16_t temp, int32_t rate_mult))
    {
        if (map_temp_mult > 0)
            rate_mult = map_temp_mult;

        return INTERPOSE_NEXT(adjustTemperature)(temp, rate_mult);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(fast_heat_hook, updateTempFromMap);
IMPLEMENT_VMETHOD_INTERPOSE(fast_heat_hook, updateTemperature);
IMPLEMENT_VMETHOD_INTERPOSE(fast_heat_hook, adjustTemperature);

static void correct_dimension(df::item_actual *self, int32_t &delta, int32_t dim)
{
    // Zero dimension or remainder?
    if (dim <= 0 || self->stack_size <= 1) return;
    int rem = delta % dim;
    if (rem == 0) return;
    // If destroys, pass through
    int intv = delta / dim;
    if (intv >= self->stack_size) return;
    // Subtract int part
    delta = rem;
    self->stack_size -= intv;
    if (self->stack_size <= 1) return;

    // If kills the item or cannot split, round up.
    if (!self->flags.bits.in_inventory || !Items::getContainer(self))
    {
        delta = dim;
        return;
    }

    // Otherwise split the stack
    color_ostream_proxy out(Core::getInstance().getConsole());
    out.print("fix-dimensions: splitting stack #%d for delta %d.\n", self->id, delta);

    auto copy = self->splitStack(self->stack_size-1, true);
    if (copy) copy->categorize(true);
}

struct dimension_lqp_hook : df::item_liquipowder {
    typedef df::item_liquipowder interpose_base;

    DEFINE_VMETHOD_INTERPOSE(bool, subtractDimension, (int32_t delta))
    {
        correct_dimension(this, delta, dimension);
        return INTERPOSE_NEXT(subtractDimension)(delta);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(dimension_lqp_hook, subtractDimension);

struct dimension_bar_hook : df::item_barst {
    typedef df::item_barst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(bool, subtractDimension, (int32_t delta))
    {
        correct_dimension(this, delta, dimension);
        return INTERPOSE_NEXT(subtractDimension)(delta);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(dimension_bar_hook, subtractDimension);

struct dimension_thread_hook : df::item_threadst {
    typedef df::item_threadst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(bool, subtractDimension, (int32_t delta))
    {
        correct_dimension(this, delta, dimension);
        return INTERPOSE_NEXT(subtractDimension)(delta);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(dimension_thread_hook, subtractDimension);

struct dimension_cloth_hook : df::item_clothst {
    typedef df::item_clothst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(bool, subtractDimension, (int32_t delta))
    {
        correct_dimension(this, delta, dimension);
        return INTERPOSE_NEXT(subtractDimension)(delta);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(dimension_cloth_hook, subtractDimension);

struct advmode_contained_hook : df::viewscreen_layer_unit_actionst {
    typedef df::viewscreen_layer_unit_actionst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *input))
    {
        auto old_reaction = cur_reaction;
        auto old_reagent = reagent;

        INTERPOSE_NEXT(feed)(input);

        if (cur_reaction && (cur_reaction != old_reaction || reagent != old_reagent))
        {
            old_reagent = reagent;

            // Skip reagents already contained by others
            while (reagent < (int)cur_reaction->reagents.size()-1)
            {
                if (!cur_reaction->reagents[reagent]->flags.bits.IN_CONTAINER)
                    break;
                reagent++;
            }

            if (old_reagent != reagent)
            {
                // Reproduces a tiny part of the orginal screen code
                choice_items.clear();

                auto preagent = cur_reaction->reagents[reagent];
                reagent_amnt_left = preagent->quantity;

                for (int i = held_items.size()-1; i >= 0; i--)
                {
                    if (!preagent->matchesRoot(held_items[i], cur_reaction->index))
                        continue;
                    if (linear_index(sel_items, held_items[i]) >= 0)
                        continue;
                    choice_items.push_back(held_items[i]);
                }

                layer_objects[6]->setListLength(choice_items.size());

                if (!choice_items.empty())
                {
                    layer_objects[4]->active = layer_objects[5]->active = false;
                    layer_objects[6]->active = true;
                }
                else if (layer_objects[6]->active)
                {
                    layer_objects[6]->active = false;
                    layer_objects[5]->active = true;
                }
            }
        }
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(advmode_contained_hook, feed);

struct fast_trade_assign_hook : df::viewscreen_layer_assigntradest {
    typedef df::viewscreen_layer_assigntradest interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *input))
    {
        if (layer_objects[1]->active && input->count(interface_key::SELECT_ALL))
        {
            set<df::interface_key> tmp; tmp.insert(interface_key::SELECT);
            INTERPOSE_NEXT(feed)(&tmp);
            tmp.clear(); tmp.insert(interface_key::STANDARDSCROLL_DOWN);
            INTERPOSE_NEXT(feed)(&tmp);
        }
        else
            INTERPOSE_NEXT(feed)(input);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(fast_trade_assign_hook, feed);

struct fast_trade_select_hook : df::viewscreen_tradegoodsst {
    typedef df::viewscreen_tradegoodsst interpose_base;

    DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *input))
    {
        if (!(is_unloading || !has_traders || in_edit_count)
            && input->count(interface_key::SELECT_ALL))
        {
            set<df::interface_key> tmp; tmp.insert(interface_key::SELECT);
            INTERPOSE_NEXT(feed)(&tmp);
            if (in_edit_count)
                INTERPOSE_NEXT(feed)(&tmp);
            tmp.clear(); tmp.insert(interface_key::STANDARDSCROLL_DOWN);
            INTERPOSE_NEXT(feed)(&tmp);
        }
        else
            INTERPOSE_NEXT(feed)(input);
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(fast_trade_select_hook, feed);

struct military_assign_hook : df::viewscreen_layer_militaryst {
    typedef df::viewscreen_layer_militaryst interpose_base;

    inline bool inPositionsMode() {
        return page == Positions && !(in_create_squad || in_new_squad);
    }

    DEFINE_VMETHOD_INTERPOSE(void, feed, (set<df::interface_key> *input))
    {
        if (inPositionsMode() && !layer_objects[0]->active)
        {
            auto pos_list = layer_objects[1];
            auto plist = layer_objects[2];
            auto &cand = positions.candidates;

            // Save the candidate list and cursors
            std::vector<df::unit*> copy = cand;
            int cursor = plist->getListCursor();
            int pos_cursor = pos_list->getListCursor();

            INTERPOSE_NEXT(feed)(input);

            if (inPositionsMode() && !layer_objects[0]->active)
            {
                bool is_select = input->count(interface_key::SELECT);

                // Resort the candidate list and restore cursor
                // on add to squad OR scroll in the position list.
                if (!plist->active || is_select)
                {
                    // Since we don't know the actual sorting order, preserve
                    // the ordering of the items in the list before keypress.
                    // This does the right thing even if the list was sorted
                    // with sort-units.
                    std::set<df::unit*> prev, next;
                    prev.insert(copy.begin(), copy.end());
                    next.insert(cand.begin(), cand.end());
                    std::vector<df::unit*> out;

                    // (old-before-cursor) (new) |cursor| (old-after-cursor)
                    for (int i = 0; i < cursor && i < (int)copy.size(); i++)
                        if (next.count(copy[i])) out.push_back(copy[i]);
                    for (size_t i = 0; i < cand.size(); i++)
                        if (!prev.count(cand[i])) out.push_back(cand[i]);
                    int new_cursor = out.size();
                    for (int i = cursor; i < (int)copy.size(); i++)
                        if (next.count(copy[i])) out.push_back(copy[i]);

                    cand.swap(out);
                    plist->setListLength(cand.size());
                    if (new_cursor < (int)cand.size())
                        plist->setListCursor(new_cursor);
                }

                // Preserve the position list index on remove from squad
                if (pos_list->active && is_select)
                    pos_list->setListCursor(pos_cursor);
            }
        }
        else
            INTERPOSE_NEXT(feed)(input);
    }

    DEFINE_VMETHOD_INTERPOSE(void, render, ())
    {
        INTERPOSE_NEXT(render)();

        if (inPositionsMode())
        {
            auto plist = layer_objects[2];
            int x1 = plist->getX1(), y1 = plist->getY1();
            int x2 = plist->getX2(), y2 = plist->getY2();
            int i1 = plist->getFirstVisible(), i2 = plist->getLastVisible();
            int si = plist->getListCursor();

            for (int y = y1, i = i1; i <= i2; i++, y++)
            {
                auto unit = vector_get(positions.candidates, i);
                if (!unit || unit->military.squad_index < 0)
                    continue;

                for (int x = x1; x <= x2; x++)
                {
                    Pen cur_tile = Screen::readTile(x, y);
                    if (!cur_tile.valid()) continue;
                    cur_tile.fg = (i == si) ? COLOR_BROWN : COLOR_GREEN;
                    Screen::paintTile(cur_tile, x, y);
                }
            }
        }
    }
};

IMPLEMENT_VMETHOD_INTERPOSE(military_assign_hook, feed);
IMPLEMENT_VMETHOD_INTERPOSE(military_assign_hook, render);

static void enable_hook(color_ostream &out, VMethodInterposeLinkBase &hook, vector <string> &parameters)
{
    if (vector_get(parameters, 1) == "disable")
    {
        hook.remove();
        out.print("Disabled tweak %s\n", parameters[0].c_str());
    }
    else
    {
        if (hook.apply())
            out.print("Enabled tweak %s\n", parameters[0].c_str());
        else
            out.printerr("Could not activate tweak %s\n", parameters[0].c_str());
    }
}

static command_result tweak(color_ostream &out, vector <string> &parameters)
{
    CoreSuspender suspend;

    if (parameters.empty())
        return CR_WRONG_USAGE;

    string cmd = parameters[0];

    if (cmd == "clear-missing")
    {
        df::unit *unit = getSelectedUnit(out, true);
        if (!unit)
            return CR_FAILURE;

        auto death = df::death_info::find(unit->counters.death_id);

        if (death)
        {
            death->flags.bits.discovered = true;

            auto crime = df::criminal_case::find(death->crime_id);
            if (crime)
                crime->flags.bits.discovered = true;
        }
    }
    else if (cmd == "clear-ghostly")
    {
        df::unit *unit = getSelectedUnit(out, true);
        if (!unit)
            return CR_FAILURE;

        // don't accidentally kill non-ghosts!
        if (unit->flags3.bits.ghostly)
        {
            // remove ghostly, set to dead instead
            unit->flags3.bits.ghostly = 0;
            unit->flags1.bits.dead = 1;
        }
        else
        {
            out.print("That's not a ghost!\n");
            return CR_FAILURE;
        }
    }
    else if (cmd == "fixmigrant")
    {
        df::unit *unit = getSelectedUnit(out, true);
        if (!unit)
            return CR_FAILURE;
        
        if(unit->race != df::global::ui->race_id)
        {
            out << "Selected unit does not belong to your race!" << endl;
            return CR_FAILURE;
        }

        // case #1: migrants who have the resident flag set
        // see http://dffd.wimbli.com/file.php?id=6139 for a save
        if (unit->flags2.bits.resident)
            unit->flags2.bits.resident = 0;
        
        // case #2: migrants who have the merchant flag
        // happens on almost all maps after a few migrant waves
        if(unit->flags1.bits.merchant)
            unit->flags1.bits.merchant = 0;

        // this one is a cheat, but bugged migrants usually have the same civ_id 
        // so it should not be triggered in most cases
        // if it happens that the player has 'foreign' units of the same race 
        // (vanilla df: dwarves not from mountainhome) on his map, just grab them
        if(unit->civ_id != df::global::ui->civ_id)
            unit->civ_id = df::global::ui->civ_id;

        return fix_clothing_ownership(out, unit);
    }
    else if (cmd == "makeown")
    {
        // force a unit into your fort, regardless of civ or race
        // allows to "steal" caravan guards etc
        df::unit *unit = getSelectedUnit(out, true);
        if (!unit)
            return CR_FAILURE;

        if (unit->flags2.bits.resident)
            unit->flags2.bits.resident = 0;
        if(unit->flags1.bits.merchant)
            unit->flags1.bits.merchant = 0;
        if(unit->flags1.bits.forest)
            unit->flags1.bits.forest = 0;
        if(unit->civ_id != df::global::ui->civ_id)
            unit->civ_id = df::global::ui->civ_id;
        if(unit->profession == df::profession::MERCHANT)
            unit->profession = df::profession::TRADER;
        if(unit->profession2 == df::profession::MERCHANT)
            unit->profession2 = df::profession::TRADER;
        return fix_clothing_ownership(out, unit);
    }
    else if (cmd == "stable-cursor")
    {
        enable_hook(out, INTERPOSE_HOOK(stable_cursor_hook, feed), parameters);
    }
    else if (cmd == "patrol-duty")
    {
        enable_hook(out, INTERPOSE_HOOK(patrol_duty_hook, isPatrol), parameters);
    }
    else if (cmd == "readable-build-plate")
    {
        if (!ui_build_selector || !ui_menu_width || !ui_area_map_width)
        {
            out.printerr("Necessary globals not known.\n");
            return CR_FAILURE;
        }

        enable_hook(out, INTERPOSE_HOOK(readable_build_plate_hook, render), parameters);
    }
    else if (cmd == "stable-temp")
    {
        enable_hook(out, INTERPOSE_HOOK(stable_temp_hook, adjustTemperature), parameters);
        enable_hook(out, INTERPOSE_HOOK(stable_temp_hook, updateContaminants), parameters);
    }
    else if (cmd == "fast-heat")
    {
        if (parameters.size() < 2)
            return CR_WRONG_USAGE;
        max_heat_ticks = atoi(parameters[1].c_str());
        if (max_heat_ticks <= 0)
            parameters[1] = "disable";
        enable_hook(out, INTERPOSE_HOOK(fast_heat_hook, updateTempFromMap), parameters);
        enable_hook(out, INTERPOSE_HOOK(fast_heat_hook, updateTemperature), parameters);
        enable_hook(out, INTERPOSE_HOOK(fast_heat_hook, adjustTemperature), parameters);
    }
    else if (cmd == "fix-dimensions")
    {
        enable_hook(out, INTERPOSE_HOOK(dimension_lqp_hook, subtractDimension), parameters);
        enable_hook(out, INTERPOSE_HOOK(dimension_bar_hook, subtractDimension), parameters);
        enable_hook(out, INTERPOSE_HOOK(dimension_thread_hook, subtractDimension), parameters);
        enable_hook(out, INTERPOSE_HOOK(dimension_cloth_hook, subtractDimension), parameters);
    }
    else if (cmd == "advmode-contained")
    {
        enable_hook(out, INTERPOSE_HOOK(advmode_contained_hook, feed), parameters);
    }
    else if (cmd == "fast-trade")
    {
        enable_hook(out, INTERPOSE_HOOK(fast_trade_assign_hook, feed), parameters);
        enable_hook(out, INTERPOSE_HOOK(fast_trade_select_hook, feed), parameters);
    }
    else if (cmd == "military-stable-assign")
    {
        enable_hook(out, INTERPOSE_HOOK(military_assign_hook, feed), parameters);
    }
    else if (cmd == "military-color-assigned")
    {
        enable_hook(out, INTERPOSE_HOOK(military_assign_hook, render), parameters);
    }
    else 
        return CR_WRONG_USAGE;

    return CR_OK;
}
