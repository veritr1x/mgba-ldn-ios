/* MPL-2.0. Cartridge codes select the real upstream wireless/cable driver. */
#ifndef IOS_GAME_PROFILE_H
#define IOS_GAME_PROFILE_H
#include <string.h>
#include <stdbool.h>
enum IOSPokemonGame { IOS_GAME_OTHER, IOS_GAME_FRLG, IOS_GAME_EMERALD, IOS_GAME_RUBY, IOS_GAME_SAPPHIRE };
enum IOSAdapterChoice { IOS_ADAPTER_OFF, IOS_ADAPTER_WIRELESS, IOS_ADAPTER_CABLE, IOS_ADAPTER_AUTO };
static inline enum IOSPokemonGame IOSGameProfile(const char code[4]) {
    if(!memcmp(code,"BPR",3) || !memcmp(code,"BPG",3))return IOS_GAME_FRLG;
    if(!memcmp(code,"BPE",3))return IOS_GAME_EMERALD;
    if(!memcmp(code,"AXV",3))return IOS_GAME_RUBY;
    if(!memcmp(code,"AXP",3))return IOS_GAME_SAPPHIRE;
    return IOS_GAME_OTHER;
}
static inline enum IOSAdapterChoice IOSResolveAdapter(int choice,enum IOSPokemonGame game) {
    if(choice>=IOS_ADAPTER_OFF && choice<=IOS_ADAPTER_CABLE)return (enum IOSAdapterChoice)choice;
    return game==IOS_GAME_RUBY || game==IOS_GAME_SAPPHIRE?IOS_ADAPTER_CABLE:IOS_ADAPTER_WIRELESS;
}
static inline bool IOSAdapterJoinOnly(enum IOSAdapterChoice adapter, enum IOSPokemonGame game) {
    return adapter==IOS_ADAPTER_CABLE || game==IOS_GAME_EMERALD;
}
#endif
