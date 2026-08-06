#include "WorldInteractionConfig.h"

FWorldInteractionSettings::FWorldInteractionSettings()
{
	SurfaceResponses.Reserve(6);
	for (int32 SurfaceIndex = 0; SurfaceIndex <= 5; ++SurfaceIndex)
	{
		FWorldSurfaceResponse& Response = SurfaceResponses.AddDefaulted_GetRef();
		Response.SurfaceType = static_cast<EPhysicalSurface>(SurfaceIndex);
	}
}
