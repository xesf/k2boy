// nirtti.cpp — see nirtti.h.
//
// The table below is generated from pop3dreal.exe by walking every
//   push <parent>; push <name>; mov ecx, <descriptor>; call NiRTTI__ctor
// sequence. Parents always precede their children, so `parent` is a
// backward index. Do not hand-edit: regenerate from the binary.

#include "engine/nirtti.h"

#include <cstring>

namespace k2 {
namespace engine {

K2_DEFINE_RTTI_ROOT(ni_object)

namespace {

const rtti_record k_table[] = {
    { "NiExtraData", -1 },
    { "Ni3dsPropAnimExtraData", 0 },
    { "NiCloneExtraData", 0 },
    { "NiStringExtraData", 0 },
    { "NiObject", -1 },
    { "ImageRef", 4 },
    { "K2ImportSet", 4 },
    { "Ni3dsLightAnimator", 4 },
    { "Ni3dsLightColorAnimator", 7 },
    { "Ni3dsPropertyAnimator", 4 },
    { "Ni3dsAlphaAnimator", 9 },
    { "Ni3dsColorAnimator", 9 },
    { "NiA3dSoundSystem", 4 },
    { "NiAVObject", 4 },
    { "ForceCollider", 13 },
    { "K2DummyObject", 13 },
    { "NiA3dListener", 13 },
    { "NiA3dSource", 13 },
    { "NiCamera", 13 },
    { "NiGeometry", 13 },
    { "NiLines", 19 },
    { "NiTriBasedGeom", 19 },
    { "NiParticles", 21 },
    { "NiAutoNormalParticles", 22 },
    { "NiTriShape", 21 },
    { "Ni3dsEnvMorphShape", 24 },
    { "Ni3dsMorphShape", 24 },
    { "Ni3dsSkin", 24 },
    { "NiEnvMappedTriShape", 24 },
    { "NiMarkerTriShape", 24 },
    { "NiTriStrip", 21 },
    { "NiTriangles", 21 },
    { "NiLight", 13 },
    { "NiProjectedLight", 32 },
    { "NiShadowLight", 32 },
    { "NiNode", 13 },
    { "AnimationNode", 35 },
    { "BlurController", 35 },
    { "Force", 35 },
    { "LinearForce", 38 },
    { "MotivateForce", 38 },
    { "RadialForce", 38 },
    { "VolumetricForce", 38 },
    { "Gauge", 35 },
    { "K2ArticulatedObject", 35 },
    { "LevelGroup", 35 },
    { "Ni3dsAnimationNode", 35 },
    { "Ni3dsBone", 46 },
    { "Ni3dsParticleSystem", 35 },
    { "Ni3dsPathController", 35 },
    { "NiBSPNode", 35 },
    { "NiBillboardNode", 35 },
    { "NiCollisionSwitch", 35 },
    { "CharacterObject", 52 },
    { "CharacterStateTable", 53 },
    { "CameraStateTable", 54 },
    { "PrinceObject", 54 },
    { "Trap", 54 },
    { "Abyss", 57 },
    { "Action", 57 },
    { "Aperture", 57 },
    { "Area", 57 },
    { "ArrowCharger", 57 },
    { "ArrowTarget", 57 },
    { "AudioMessage", 57 },
    { "Bar", 57 },
    { "CameraMessage", 57 },
    { "CollapsingFloor", 57 },
    { "Crate", 57 },
    { "CrateTarget", 57 },
    { "DelayMessage", 57 },
    { "Door", 57 },
    { "EnemyMessage", 57 },
    { "EnemyStart", 57 },
    { "Fixture", 57 },
    { "FlameThrower", 57 },
    { "ForceTrap", 57 },
    { "Fountain", 57 },
    { "GameMessage", 57 },
    { "Generic", 57 },
    { "Launcher", 57 },
    { "LightSwitch", 57 },
    { "Messenger", 57 },
    { "Mover", 57 },
    { "Pickup", 57 },
    { "Pole", 57 },
    { "PressurePlate", 57 },
    { "PrinceMessage", 57 },
    { "PrinceStart", 57 },
    { "RoomAttributes", 57 },
    { "Rope", 57 },
    { "SpecialEffect", 57 },
    { "Specials", 57 },
    { "Spikes", 57 },
    { "VideoMessage", 57 },
    { "WallBlade", 57 },
    { "Water", 57 },
    { "NiMarkerNode", 35 },
    { "k2AnimatedTextureHub", 97 },
    { "k2AnimationPropertyHub", 97 },
    { "NiSwitchNode", 35 },
    { "NiFltAnimationNode", 100 },
    { "NiLODNode", 100 },
    { "RoomGroup", 35 },
    { "SimpleTrap", 35 },
    { "SingleSpurt", 35 },
    { "SingleSpurtPuddle", 35 },
    { "k2FileEffectController", 35 },
    { "k2SpecialEffectController", 107 },
    { "k2MoveableController", 108 },
    { "k2ArrowTrail", 109 },
    { "k2PostSceneNode", 35 },
    { "k2SceneGraph", 35 },
    { "k2WeatherNode", 35 },
    { "k2LinkerAVObject", 13 },
    { "NiAction", 4 },
    { "NiCameraAction", 115 },
    { "NiFlipTextures", 115 },
    { "NiUserAction", 115 },
    { "k2AnimationAction", 115 },
    { "k2AnimationCoreAction", 119 },
    { "k2AnimationUpdateAction", 120 },
    { "k2ParticleSystemAction", 121 },
    { "k2PSChadAction", 122 },
    { "k2PSExtraStepAction", 122 },
    { "k2NetImmerseAction", 119 },
    { "k2TextureAction", 125 },
    { "k2SpecialEffectManager", 115 },
    { "NiDisplay", 4 },
    { "NiForce", 4 },
    { "NiGravity", 129 },
    { "NiMeshCollider", 129 },
    { "NiParticleBomb", 129 },
    { "NiPlanarCollider", 129 },
    { "NiSphericalCollider", 129 },
    { "NiImage", 4 },
    { "NiDDImage", 135 },
    { "NiDDBufferImage", 136 },
    { "NiPalette", 4 },
    { "NiProperty", 4 },
    { "NiAlphaProperty", 139 },
    { "NiCorrectionProperty", 139 },
    { "NiDitherProperty", 139 },
    { "NiFogProperty", 139 },
    { "NiMaterialProperty", 139 },
    { "NiMultiTextureProperty", 139 },
    { "NiShadeProperty", 139 },
    { "NiSpecularProperty", 139 },
    { "NiTextureModeProperty", 139 },
    { "NiTextureProperty", 139 },
    { "NiTransparentProperty", 139 },
    { "NiVertexColorProperty", 139 },
    { "NiWireframeProperty", 139 },
    { "NiZBufferProperty", 139 },
    { "NiRawImageData", 4 },
    { "NiRender", 4 },
    { "NiD3DRender", 155 },
    { "NiScreenPolygon", 4 },
    { "Indicator", 157 },
    { "NiUser", 4 },
    { "PropertyBase", 4 },
    { "PropArtic", 160 },
    { "PropForce", 160 },
    { "PropLOD", 160 },
    { "PropPortal", 160 },
    { "PropTrap", 160 },
    { "SpecialEffectProp", 160 },
    { "BillboardProp", 166 },
    { "WeaponParameters", 4 },
};

} // namespace

const rtti_record* rtti_table(size_t& count) {
    count = sizeof(k_table) / sizeof(k_table[0]);
    return k_table;
}

int rtti_find(const char* name) {
    if (!name) return -1;
    const size_t n = sizeof(k_table) / sizeof(k_table[0]);
    for (size_t i = 0; i < n; ++i)
        if (!std::strcmp(k_table[i].name, name)) return int(i);
    return -1;
}

bool rtti_is_kind_of(const char* name, const char* base) {
    const int b = rtti_find(base);
    if (b < 0) return false;
    for (int i = rtti_find(name); i >= 0; i = k_table[i].parent)
        if (i == b) return true;
    return false;
}

} // namespace engine
} // namespace k2
