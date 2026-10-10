#include "../OptiScaler/inputs/NgxFeatureRegistry.h"
#include <cassert>
#include <iostream>
#include <thread>

int main()
{
    NgxFeatureRegistry registry;
    assert(!registry.Read(100).feature);
    registry.Record(100, NVSDK_NGX_Feature_RayReconstruction);
    registry.Record(200, NVSDK_NGX_Feature_FrameGeneration);
    assert(registry.Read(100).feature == NVSDK_NGX_Feature_RayReconstruction);
    assert(registry.Read(100).frameGenerationCreated);
    registry.Forget(200);
    assert(!registry.Read(100).frameGenerationCreated);
    registry.Forget(100);
    assert(!registry.Read(100).feature);
    registry.Record(100, NVSDK_NGX_Feature_SuperSampling);
    assert(registry.Read(100).feature == NVSDK_NGX_Feature_SuperSampling);
    std::thread writer(
        [&]
        {
            for (unsigned int i = 0; i < 1000; ++i)
            {
                registry.Record(200, NVSDK_NGX_Feature_FrameGeneration);
                registry.Forget(200);
            }
        });
    for (unsigned int i = 0; i < 1000; ++i)
        assert(registry.Read(100).feature == NVSDK_NGX_Feature_SuperSampling);
    writer.join();
    assert(!registry.Read(100).frameGenerationCreated);

    std::cout << "NGX handle lifecycle passed\n";
}
