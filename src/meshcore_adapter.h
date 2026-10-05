#pragma once

// This is MeshInk's single application-facing include for the upstream
// companion example type. Keep upstream source unmodified and isolate any
// MyMesh/example compatibility work here or in companion_runtime.cpp.
//
// Rename the stock example's global declaration while importing the header so
// application code never binds to MeshCore's example-global `the_mesh`.
#define the_mesh meshcore_upstream_example_the_mesh
#include "../lib/MeshCore/examples/companion_radio/MyMesh.h"
#undef the_mesh

MyMesh& meshink_meshcore();
