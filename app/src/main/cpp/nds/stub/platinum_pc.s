// PC anywhere for Platinum (QUICK MENU / BAG > PC): the Pokémon Storage System's own menu, as a Pokémon Center PC
// opens it after "Accessed the Pokémon Storage System" - the game's CommonScript_StorageSystem (scripts_common.s)
// without the PC's boot / shut-down animation (there's no PC on the map) and without the PC's first menu: B or SEE YA
// logs off. Assembled with pret/pokeplatinum's script macros by scripts/gen_nds_stub.py; the game reads it from the
// ITCM (PK_NDS_SCRIPT_ADDR), started by pk_nds_call's PC job.
#include "macros/scrcmd.inc"
#include "res/text/bank/common_strings.h"
#include "res/text/bank/menu_entries.h"

    .data

PCAnywhere:
    LockAll
    PlaySE SEQ_SE_DP_PC_LOGIN_sseq
    BufferPlayerName 0
    Message CommonStrings_Text_AccessedPokemonStorage
    Call PCAnywhere_InitMenu
    GoTo PCAnywhere_HandleInput

PCAnywhere_InitMenu:
    InitGlobalTextListMenu 1, 1, 0, VAR_RESULT
    AddListMenuEntry MenuEntries_Text_PC_DepositPokemon, 0, MenuEntries_Text_PC_DescriptionDepositPokemon
    AddListMenuEntry MenuEntries_Text_PC_WithdrawPokemon, 1, MenuEntries_Text_PC_DescriptionWithdrawPokemon
    AddListMenuEntry MenuEntries_Text_PC_MovePokemon, 2, MenuEntries_Text_PC_DescriptionMovePokemon
    AddListMenuEntry MenuEntries_Text_PC_MoveItems, 3, MenuEntries_Text_PC_DescriptionMoveItems
    CallIfSet FLAG_CONTEST_HALL_VISITED, PCAnywhere_AddCompare
    AddListMenuEntry MenuEntries_Text_PC_SeeYa, 5, MenuEntries_Text_PC_DescriptionSeeYa
    Return

PCAnywhere_AddCompare:
    AddListMenuEntry MenuEntries_Text_PC_ComparePokemon, 4, MenuEntries_Text_PC_DescriptionComparePokemon
    Return

PCAnywhere_HandleInput:
    ShowListMenu
    SetVar VAR_0x8008, VAR_RESULT
    GoToIfEq VAR_0x8008, 0, PCAnywhere_Deposit
    GoToIfEq VAR_0x8008, 1, PCAnywhere_Withdraw
    GoToIfEq VAR_0x8008, 2, PCAnywhere_Move
    GoToIfEq VAR_0x8008, 3, PCAnywhere_MoveItems
    GoToIfEq VAR_0x8008, 4, PCAnywhere_Compare
    GoTo PCAnywhere_LogOff

PCAnywhere_Deposit:
    CloseMessage
    Call PCAnywhere_FadeOut
    OpenPokemonStorage 0
    ReturnToField
    GoTo PCAnywhere_FadeIn

PCAnywhere_Withdraw:
    CloseMessage
    Call PCAnywhere_FadeOut
    OpenPokemonStorage 1
    ReturnToField
    GoTo PCAnywhere_FadeIn

PCAnywhere_Move:
    CloseMessage
    Call PCAnywhere_FadeOut
    OpenPokemonStorage 2
    ReturnToField
    GoTo PCAnywhere_FadeIn

PCAnywhere_MoveItems:
    CloseMessage
    Call PCAnywhere_FadeOut
    OpenPokemonStorage 3
    ReturnToField
    GoTo PCAnywhere_FadeIn

PCAnywhere_Compare:
    CloseMessage
    Call PCAnywhere_FadeOut
    OpenPokemonStorage 4
    ReturnToField
    GoTo PCAnywhere_FadeIn

PCAnywhere_FadeOut:
    FadeScreenOut
    WaitFadeScreen
    Return

PCAnywhere_FadeIn:
    SaveTVSegmentPokemonStorageBulletin
    BufferPlayerName 0
    MessageInstant CommonStrings_Text_AccessedPokemonStorage
    Call PCAnywhere_InitMenu
    FadeScreenIn
    WaitFadeScreen
    GoTo PCAnywhere_HandleInput

PCAnywhere_LogOff:
    CloseMessage
    PlaySE SEQ_SE_DP_PC_LOGOFF_sseq
    ReleaseAll
    End
