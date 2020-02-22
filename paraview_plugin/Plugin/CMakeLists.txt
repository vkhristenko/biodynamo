set(sources
  BDMGlyph.cc
  BDMGlyph.h
  BDMGlyphFilter.cc
  BDMGlyphFilter.cc)

paraview_add_plugin(BDMGlyphFilter
  VERSION "1.0" 
  SERVER_MANAGER_XML BDMGlyphFilter.xml
  SOURCES ${sources})
