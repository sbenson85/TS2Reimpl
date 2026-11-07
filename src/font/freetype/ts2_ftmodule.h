// The FreeType modules our build compiles (FT_CONFIG_MODULES_H, CMakeLists.txt "FreeType"): the drivers for the font
// kinds Font Fusion read (TrueType and OpenType, Type 1, bare CFF), the PostScript helpers they need, the hinters and
// the two renderers (smooth for 8-bit glyphs, raster for 1-bit ones).
FT_USE_MODULE(FT_Module_Class, autofit_module_class)
FT_USE_MODULE(FT_Driver_ClassRec, tt_driver_class)
FT_USE_MODULE(FT_Driver_ClassRec, t1_driver_class)
FT_USE_MODULE(FT_Driver_ClassRec, cff_driver_class)
FT_USE_MODULE(FT_Module_Class, psaux_module_class)
FT_USE_MODULE(FT_Module_Class, psnames_module_class)
FT_USE_MODULE(FT_Module_Class, pshinter_module_class)
FT_USE_MODULE(FT_Module_Class, sfnt_module_class)
FT_USE_MODULE(FT_Renderer_Class, ft_smooth_renderer_class)
FT_USE_MODULE(FT_Renderer_Class, ft_raster1_renderer_class)
