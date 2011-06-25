#include <stdint.h>
#include <iostream>
#include <map>
#include <vector>
#include <dfhack/Core.h>
#include <dfhack/Console.h>
#include <dfhack/Export.h>
#include <dfhack/PluginManager.h>
#include <dfhack/modules/Maps.h>
#include <dfhack/modules/World.h>

using namespace DFHack;

struct hideblock
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint8_t hiddens [16][16];
};

// the saved data. we keep map size to check if things still match
uint32_t x_max, y_max, z_max;
std::vector <hideblock> hidesaved;
bool revealed = false;

DFhackCExport command_result reveal(DFHack::Core * c, std::vector<std::string> & params);
DFhackCExport command_result unreveal(DFHack::Core * c, std::vector<std::string> & params);
DFhackCExport command_result revealtoggle(DFHack::Core * c, std::vector<std::string> & params);
//DFhackCExport command_result revealclear(DFHack::Core * c, std::vector<std::string> & params);

DFhackCExport const char * plugin_name ( void )
{
    return "reveal";
}

DFhackCExport command_result plugin_init ( Core * c, std::vector <PluginCommand> &commands)
{
    commands.clear();
    commands.push_back(PluginCommand("reveal","Reveal the map.",reveal));
    commands.push_back(PluginCommand("unreveal","Revert the map to its previous state.",unreveal));
    commands.push_back(PluginCommand("revealtoggle","Reveal/unreveal depending on state.",revealtoggle));
    //commands.push_back(PluginCommand("revealclear","Reset the reveal tool.",revealclear));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( Core * c )
{
    return CR_OK;
}

DFhackCExport command_result reveal(DFHack::Core * c, std::vector<std::string> & params)
{
    DFHack::designations40d designations;
    if(revealed)
    {
        dfout << "Map is already revealed or this is a different map." << std::endl;
        return CR_FAILURE;
    }
    c->Suspend();
    DFHack::Maps *Maps =c->getMaps();
    DFHack::World *World =c->getWorld();
    // init the map
    if(!Maps->Start())
    {
        dfout << "Can't init map." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }

    dfout << "Revealing, please wait..." << std::endl;
    Maps->getSize(x_max,y_max,z_max);
    hidesaved.reserve(x_max * y_max * z_max);
    for(uint32_t x = 0; x< x_max;x++)
    {
        for(uint32_t y = 0; y< y_max;y++)
        {
            for(uint32_t z = 0; z< z_max;z++)
            {
                if(Maps->isValidBlock(x,y,z))
                {
                    hideblock hb;
                    hb.x = x;
                    hb.y = y;
                    hb.z = z;
                    // read block designations
                    Maps->ReadDesignations(x,y,z, &designations);
                    // change the hidden flag to 0
                    for (uint32_t i = 0; i < 16;i++) for (uint32_t j = 0; j < 16;j++)
                    {
                        hb.hiddens[i][j] = designations[i][j].bits.hidden;
                        designations[i][j].bits.hidden = 0;
                    }
                    hidesaved.push_back(hb);
                    // write the designations back
                    Maps->WriteDesignations(x,y,z, &designations);
                }
            }
        }
    }
    World->SetPauseState(true);
    revealed = true;
    c->Resume();
    dfout << "Map revealed. The game has been paused for you." << std::endl;
    dfout << "Unpausing can unleash the forces of hell!" << std::endl;
    dfout << "Saving will make this state permanent. Don't do it." << std::endl << std::endl;
    dfout << "Run 'reveal' again to revert to previous state." << std::endl;
    return CR_OK;
}

DFhackCExport command_result unreveal(DFHack::Core * c, std::vector<std::string> & params)
{
    DFHack::designations40d designations;
    if(!revealed)
    {
        dfout << "There's nothing to revert!" << std::endl;
        return CR_FAILURE;
    }
    c->Suspend();
    DFHack::Maps *Maps =c->getMaps();
    DFHack::World *World =c->getWorld();
    Maps = c->getMaps();
    if(!Maps->Start())
    {
        dfout << "Can't init map." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }

    // Sanity check: map size
    uint32_t x_max_b, y_max_b, z_max_b;
    Maps->getSize(x_max_b,y_max_b,z_max_b);
    if(x_max != x_max_b || y_max != y_max_b || z_max != z_max_b)
    {
        dfout << "The map is not of the same size..." << std::endl;
        c->Resume();
        return CR_FAILURE;
    }

    // FIXME: add more sanity checks / MAP ID

    dfout << "Unrevealing... please wait." << std::endl;
    for(size_t i = 0; i < hidesaved.size();i++)
    {
        hideblock & hb = hidesaved[i];
        Maps->ReadDesignations(hb.x,hb.y,hb.z, &designations);
        for (uint32_t i = 0; i < 16;i++) for (uint32_t j = 0; j < 16;j++)
        {
            designations[i][j].bits.hidden = hb.hiddens[i][j];
        }
        Maps->WriteDesignations(hb.x,hb.y,hb.z, &designations);
    }
    // give back memory.
    hidesaved.clear();
    revealed = false;
    c->Resume();
    return CR_OK;
}

DFhackCExport command_result revealtoggle (DFHack::Core * c, std::vector<std::string> & params)
{
    if(revealed)
    {
        return unreveal(c,params);
    }
    else
    {
        return reveal(c,params);
    }
}
