// PC anywhere for HeartGold / SoulSilver (QUICK MENU / BAG > PC): the Pokémon Storage System's own menu, as a Pokémon
// Center PC opens it after "The Pokémon Storage System was accessed" - the game's std_pokecenter_pc (scr_seq_0003.s)
// without the PC's screen animation (ScrCmd_500 / 501 / 308 / 309 / 502 on the PC prop: there's no PC on the map, and
// the game's own script crashed there) and without the PC's first menu: SWITCH OFF or B logs off. Assembled with
// pret/pokeheartgold's script macros by scripts/gen_nds_stub.py; the game reads it from the ITCM (PK_NDS_SCRIPT_ADDR),
// started by pk_nds_call's PC job in the context of std_pokecenter_pc (scr_seq_0003, text bank msg_0040).
#include "constants/scrcmd.h"
#include "msgdata/msg/msg_0040.h"
#include "msgdata/msg/msg_0191.h"
#include "macros/script.inc"

PCAnywhere:
	ScrCmd_609
	LockAll
	TouchscreenMenuHide
	PlaySE SEQ_SE_DP_PC_LOGIN
	BufferPlayersName 0
	NPCMsg msg_0040_00035
	Call PCAnywhere_InitMenu
	GoTo PCAnywhere_HandleInput

PCAnywhere_InitMenu:
	MenuInitStdGmm 1, 1, 0, 1, VAR_SPECIAL_RESULT
	MenuItemAdd msg_0191_00067, 76, 0
	MenuItemAdd msg_0191_00068, 77, 1
	MenuItemAdd msg_0191_00069, 78, 2
	MenuItemAdd msg_0191_00070, 79, 3
	MenuItemAdd msg_0191_00072, 81, 5
	Return

PCAnywhere_HandleInput:
	MenuExec
	Switch VAR_SPECIAL_RESULT
	Case 0, PCAnywhere_Deposit
	Case 1, PCAnywhere_Withdraw
	Case 2, PCAnywhere_Move
	Case 3, PCAnywhere_MoveItems
	Case 4, PCAnywhere_Compare
	GoTo PCAnywhere_LogOff

PCAnywhere_Deposit:
	CloseMsg
	Call PCAnywhere_FadeOut
	ScrCmd_158 0
	RestoreOverworld
	GoTo PCAnywhere_Back

PCAnywhere_Withdraw:
	CloseMsg
	Call PCAnywhere_FadeOut
	ScrCmd_158 1
	RestoreOverworld
	GoTo PCAnywhere_Back

PCAnywhere_Move:
	CloseMsg
	Call PCAnywhere_FadeOut
	ScrCmd_158 2
	RestoreOverworld
	GoTo PCAnywhere_Back

PCAnywhere_MoveItems:
	CloseMsg
	Call PCAnywhere_FadeOut
	ScrCmd_158 3
	RestoreOverworld
	GoTo PCAnywhere_Back

PCAnywhere_Compare:
	CloseMsg
	Call PCAnywhere_FadeOut
	ScrCmd_158 4
	RestoreOverworld
	GoTo PCAnywhere_Back

PCAnywhere_Back:
	BufferPlayersName 0
	NonNPCMsg msg_0040_00035
	Call PCAnywhere_InitMenu
	FadeScreen 6, 1, 1, RGB_BLACK
	WaitFade
	GoTo PCAnywhere_HandleInput

PCAnywhere_FadeOut:
	FadeScreen 6, 1, 0, RGB_BLACK
	WaitFade
	Return

PCAnywhere_LogOff:
	CloseMsg
	PlaySE SEQ_SE_DP_PC_LOGOFF
	TouchscreenMenuShow
	ReleaseAll
	End
