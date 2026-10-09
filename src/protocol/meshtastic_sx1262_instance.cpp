// Instantiate only the pinned upstream SX1262 driver template.
// Upstream InterfacesTemplates.cpp also instantiates unrelated radios/servers.
#include "../../lib/Meshtastic/src/mesh/SX126xInterface.cpp"
#if RADIOLIB_EXCLUDE_SX126X != 1
template class SX126xInterface<SX1262>;
#endif
