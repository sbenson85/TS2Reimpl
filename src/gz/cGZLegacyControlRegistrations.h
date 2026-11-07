#pragma once
// The window classes' registrations of cGZLegacyControl (lane ME, foundations F6 part 1; src/gz/cGZLegacyControl.h
// explains the three tables). The first control's constructor runs all 22, in this order (RunRegistrations); each is
// cdecl, no arguments, and needs the shared vocabulary (cGZLegacyControl::kVocabCell) set.

// 0x85e431: clsid
void __cdecl RegisterCommonNames();
// 0x86024e: GZWinFlatRect
void __cdecl RegisterFlatRect();
// 0x86249d: GZWinText
void __cdecl RegisterText();
// 0x86288d: GZWinTextEdit
void __cdecl RegisterTextEdit();
// 0x861263: GZWinListBox
void __cdecl RegisterListBox();
// 0x85e28d: GZWinCombo
void __cdecl RegisterCombo();
// 0x85dba9: GZWinBtn
void __cdecl RegisterButton();
// 0x85da3d: GZWinBMP
void __cdecl RegisterBitmap();
// 0x861fd1: GZWinSlider
void __cdecl RegisterSlider();
// 0x861e67: GZWinScrollbar
void __cdecl RegisterScrollbar();
// 0x860f01: GZWinLineInput
void __cdecl RegisterLineInput();
// 0x8614af: GZWinOptGrp
void __cdecl RegisterOptionGroup();
// 0x862173: GZWinSpinner
void __cdecl RegisterSpinner();
// 0x861999: GZWinOutline
void __cdecl RegisterOutline();
// 0x862fa9: GZWinTextTicker
void __cdecl RegisterTextTicker();
// 0x85f31a: GZWinFileBrowser
void __cdecl RegisterFileBrowser();
// 0x863115: GZWinTreeView
void __cdecl RegisterTreeView();
// 0x860508: GZWinFolders
void __cdecl RegisterFolders();
// 0x8605b7: GZWinGrid
void __cdecl RegisterGrid();
// 0x85f26b: GZWinCustom
void __cdecl RegisterCustom();
// 0x861b05: GZWinScrollbar2
void __cdecl RegisterScrollbar2();
// 0x8635c7: SC3WinGen
void __cdecl RegisterGen();
