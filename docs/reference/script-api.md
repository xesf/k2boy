# Piccolo script API — recovered from `data/pop3dreal.exe`

**Status: confirmed.** Extracted by scanning `.text` for `push imm32` operands pointing at the
reflection strings, in address order. Each script class has a registrar routine that pushes, per
method, the qualified name (`Class::Method`) followed by its parameter descriptors (`Type name`),
plus property descriptors (`prop - Type (read only)`).

The table is **built by code at runtime**, not stored as a static array — pointers to these
strings appear only in `.text`, never in `.data`. `xrefs_to` on the strings therefore lands in
registrar functions (`MfScFunctionRegistry` / `MfScClassInfo` construction), which is also why
IDA's `xrefs_to` on the string addresses is the right tool but on the RTTI addresses is not.

- script-visible classes: **29**
- methods: **250** (246 distinct names)
- parameter descriptors: **241**
- properties: **83**

### Extraction caveats

String literals are linker-pooled, so a parameter string is shared across every method that uses
the same `Type name` pair. Attribution here is by **push order**: a `PARAM` belongs to the most
recent `QUAL` within 0x300 bytes. Two filters remove false positives:

- parameter names must start lowercase (real ones do: `transitionName`, `goal`, `x`); this
  rejects unrelated two-word strings such as `Floor Graph` and `State Error` that sit in the
  same registrar functions
- a distance cutoff stops attribution bleeding past the end of a registrar

Long parameter lists on one name are **overload sets** — the registrar pushes every accepted
signature (e.g. `Vector3.Vector3` takes either a `Vector3` or `float x, y, z`).

Signatures are therefore high-confidence on *names* and *types present*, and lower-confidence on
*exact arity per overload*. Verify a specific method against its registrar before relying on arity.

## Registrar functions (named in the IDA database)

One registrar per script class. Renamed `Script_Register_<Class>`:

| Address | Name | Size |
|---|---|---|
| `0x69BE17` | `Script_Register_Actor` | 0xBAF2 |
| `0x6C3033` | `Script_Register_Actor_part2` | 0x3F5 |
| `0x6B8733` | `Script_Register_Activity` | 0x16EB |
| `0x6B2440` | `Script_Register_ActorNode` | 0xED7 |
| `0x659B93` | `Script_Register_App` | 0x4C2 |
| `0x6C12FA` | `Script_Register_Color` | 0x5A9 |
| `0x6D606F` | `Script_Register_Console` | 0x443 |
| `0x698AD0` | `Script_Register_Event` | 0x46A |
| `0x6BC04B` | `Script_Register_Filter` | 0x46B |
| `0x65C2A0` | `Script_Register_HFSM` | 0x372 |
| `0x62BCAA` | `Script_Register_Joystick` | 0xDAF |
| `0x6C4457` | `Script_Register_KChain` | 0x6A9 |
| `0x6BE8E0` | `Script_Register_Keyboard` | 0xBB2 |
| `0x6B4817` | `Script_Register_Material` | 0xB16 |
| `0x6BDBED` | `Script_Register_Mouse` | 0x708 |
| `0x6C3AA2` | `Script_Register_Path2` | 0x4F5 |
| `0x6CA8C7` | `Script_Register_Piccolo` | 0x6F1 |
| `0x6C1E13` | `Script_Register_PoList` | 0x511 |
| `0x6C7991` | `Script_Register_Rotation3` | 0x951 |
| `0x6C5163` | `Script_Register_Skill` | 0x894 |
| `0x6583CE` | `Script_Register_Sound` | 0xD2A |
| `0x65A4FC` | `Script_Register_SoundDriver` | 0x515 |
| `0x65AEAE` | `Script_Register_SoundResource` | 0xB72 |
| `0x6536C4` | `Script_Register_Space` | 0x10F6 |
| `0x6D6E47` | `Script_Register_String` | 0x1236 |
| `0x6BCB09` | `Script_Register_Texture` | 0x4C3 |
| `0x6B6C65` | `Script_Register_Vector3` | 0xD04 |
| `0x62ECBC` | `Script_Register_View` | 0xCAF |

**Two registrars are not yet defined as functions** in the database — IDA's auto-analysis did not
reach them:

- `Transform` — first QUAL push at `0x6C6568`
- `World` — first QUAL push at `0x656032`

Run `define_func` at the containing region (or scroll back to the prologue) to recover those two.
Their method names are already in this document; only the registrar body is missing.

`Script_Register_Actor` is **0xBAF2 bytes** — 47 KB in a single function, holding 63 of Actor's 68
methods. `Hexrays` fails on `Script_Register_HFSM` (`hx:GenPseudo` error), so read these as
disassembly, not pseudocode.

---

## `Actor` — 68 methods

Properties:

- `className - String (read only)`
- `cylinderRadius - float (read only)`
- `instanceName - String (read only)`
- `nodeUnderRoot - ActorNode (read only)`
- `numNodes - int (read only)`
- `primaryNode - ActorNode (read only)`
- `rootNode - ActorNode (read only)`

```
Actor.ActivateBehavior(String behaviorName)
Actor.AddActivity(Activity activity)
Actor.AddActivity()
Actor.AddFilterItem(Filter item)
Actor.AttachTarget(Actor targetActor, String nodeName)
Actor.ChangeTexture(String nodeName, int materialIndex, String textureFile)
Actor.ClearActivity()
Actor.ClearFilter()
Actor.CorrectPlacement()
Actor.Create3dPathActivity(String locoSkillName, float pathLength, int options, String locoSkillName)
Actor.CreateAttachActivity(String skillName, Transform attachXform, int options)
Actor.CreateCameraActivity(String cameraNode, Actor targetActor, String targetNode, String cameraNode)
Actor.CreateComplyActivity(Actor leaderActor, String leaderNode, String nodeName, int chainLength)
Actor.CreateGestureActivity(String skillName, int options)
Actor.CreateHoldActivity(Actor targetActor)
Actor.CreateKChain(String endNodeName, int chainLength)
Actor.CreateLocoActivity(String locoSkillName, float pathLength, float maxRise, float maxFall, int options)
Actor.CreateOrientActivity(String endNode, Vector3 orientVector, int chainLength, Actor targetActor, String targetNode)
Actor.CreateOverlayActivity()
Actor.CreateRandomActivity(int playbackCount)
Actor.CreateSequentialActivity(int playbackCount)
Actor.DeactivateBehavior(String behaviorName)
Actor.DeleteFilterItem(Filter item)
Actor.DetachTarget(Actor targetActor)
Actor.DisableToolMouseBehavior()
Actor.DisplayText()
Actor.EnableToolMouseBehavior()
Actor.FacePoint(Vector3 facePoint)
Actor.FlushAllSkills()
Actor.FlushSkill(String skillName)
Actor.GetChildActors()
Actor.GetCurStateName(String hfsmName, String fullPathName, String curStateName)
Actor.GetLastCollisionInfo(CollisionData data)
Actor.GetLastContourFailInfo(ContourFailData data)
Actor.GetLastPickInfo(PickData data)
Actor.GetLastProximityInfo(ProximityData data)
Actor.GetNodeByIndex(int index)
Actor.GetNodeByName(String name)
Actor.GetSkill(String name)
Actor.InstantiatePluginActivity(PluginActivity activity)
Actor.LoadAllSkills()
Actor.LoadBehavior(String behaviorName, boolean activate)
Actor.LoadSkill(String skillName, boolean lock)
Actor.RemoveActivity(Activity activity, int actID)
Actor.RequestGraspPlan(Actor targetActor, String graspSkillName, String poseSkillName, String locoSkillName, String stopSkillName)
Actor.RequestLocoPlan(String locoSkillName, String stopSkillName, Vector3 goalPosition, String locoSkillName)
Actor.RequestPlacePlan(Actor targetActor, Vector3 goalPosition, String placeSkillName, String poseSkillName, String locoSkillName, String stopSkillName)
Actor.RequestPosePlan(Actor targetActor, String poseSkillName)
Actor.RequestSitPlan(Actor targetActor, String sitSkillName, String locoSkillName, String stopSkillName)
Actor.RequestTouchPlan(Actor targetActor, String touchSkillName, String poseSkillName, String locoSkillName, String stopSkillName)
Actor.RequestUngraspPlan(Actor targetActor, String ungraspSkillName)
Actor.SampleAttachSkill(String skillName, Transform attachXform, int sampleTime)
Actor.SendEvent(Actor receiver, String eventName, int delay, Actor receiver, Event event)
Actor.SendMsg(Actor receiver, String msg)
Actor.SetActivity(Activity activity)
Actor.SetActivity()
Actor.SetForwardFacingDirection(Rotation3 orientation)
Actor.SetPlannedActivityOptions(int activityType, int option)
Actor.SetProximityDefinition(String protoName, float radius, float width, float length, BOOL enabled)
Actor.SetupSkillCache(int size)
Actor.SnapTraceToSurface(Vector3 direction)
Actor.StartTimer(int timerID, String name, int delay, boolean isPeriodic, int timerID)
Actor.StopTimer(int timeID)
Actor.SyncTraceUnderHip()
Actor.UndoSample()
Actor.UnloadBehavior(String behaviorName)
```

## `Sound` — 28 methods

```
Sound.Beep()
Sound.ClearAllSounds()
Sound.ClearSound()
Sound.GetConeAmbient()
Sound.GetConeSize()
Sound.GetFrequency()
Sound.GetListenerVelocity()
Sound.GetPan()
Sound.GetRoomSize()
Sound.GetSoundDuration()
Sound.GetSourceVelocity()
Sound.GetSpeedOfSound()
Sound.GetVolume()
Sound.LoadSound()
Sound.PlaySound()
Sound.SetAttenuation()
Sound.SetConeAmbient()
Sound.SetConeSize()
Sound.SetFrequency()
Sound.SetListenerNode()
Sound.SetListenerVelocity()
Sound.SetPan()
Sound.SetRoomSize()
Sound.SetSourceVelocity()
Sound.SetSpeedOfSound()
Sound.SetVolume()
Sound.StopSound()
Sound.UnloadSound()
```

## `Piccolo` — 16 methods

```
Piccolo.AddVariable()
Piccolo.CallStack()
Piccolo.DoesVariableExist()
Piccolo.FlushFunctions()
Piccolo.GetClassName()
Piccolo.GetDataPath()
Piccolo.GetFunctionsInDataPath()
Piccolo.GetInternalClassName()
Piccolo.LoadFunctions()
Piccolo.QueryPropertyNames()
Piccolo.RemoveVariable()
Piccolo.SetDataPath()
Piccolo.firstExtension()
Piccolo.firstPlugin()
Piccolo.nextExtension()
Piccolo.nextPlugin()
```

## `String` — 16 methods

```
String.Ascii()
String.CaseSubString()
String.CharsInSet()
String.LeftString()
String.MidString()
String.Print()
String.PrintNl()
String.RightString()
String.StripLeadingWhite()
String.StripTrailingWhite()
String.StripWhite()
String.SubString()
String.ToFloat(int intValue, String subString, int startPos)
String.ToInt()
String.ToLower()
String.ToUpper()
```

## `Activity` — 12 methods

Properties:

- `blendTime - int`
- `duration - int`
- `type - int (read only)`
- `uniqueID - int (read only)`

```
Activity.AddActivity(Activity child)
Activity.AnimateSubtree(String nodeName)
Activity.CreatePathCopy()
Activity.GetSubactivities()
Activity.IgnoreSubtree(String nodeName)
Activity.RemoveHoldActivity(Actor targetActor)
Activity.RemoveSubactivity(Activity activity)
Activity.SetBoundary(boolean useBoundary, Vector3 boundaryNormal, Vector3 boundaryPoint)
Activity.SetCameraUpVector(boolean useFixedUp, Vector3 upVector)
Activity.SetFocus()
Activity.SetStiffness(int whichChannel, float stiffness)
Activity.TestViewDirection(int viewIndex)
```

## `PoList` — 12 methods

```
PoList.Append()
PoList.Clear()
PoList.Current()
PoList.Delete()
PoList.Find()
PoList.First()
PoList.GetItem()
PoList.Last()
PoList.Next()
PoList.Prepend()
PoList.Prev()
PoList.SetItem()
```

## `World` — 12 methods

Properties:

- `lastLoadedSpace - Space (read only)`

```
World.DeleteActor(Actor actor)
World.DeleteSpace(String spaceName)
World.DisableToolMouseBehavior()
World.EnableToolMouseBehavior()
World.LoadActor(String actorName, Space space)
World.LoadSpace(String spaceName)
World.Reset()
World.SaveActor(Actor actor)
World.SaveSpace(String spaceName)
World.Stop()
World.TeleportActor(Actor actor, String spaceName)
World.UpdateViews()
```

## `Material` — 8 methods

Properties:

- `ambientColor - Color`
- `diffuseColor - Color`
- `emissiveColor - Color`
- `opacity - float`
- `opacityMap - String`
- `shininess - float`
- `textureMap - String`

```
Material.CreateTextureCopy(int textureIndex)
Material.GetColor()
Material.GetSpecularColor()
Material.GetSurface()
Material.SetColor()
Material.SetSpecularColor()
Material.SetSurface()
Material.SetTexture(int textureIndex, Texture texture)
```

## `Space` — 8 methods

Properties:

- `lastAddedActor - Actor (read only)`
- `spaceName - String (read only)`

```
Space.AttachCameraView(ActorNode camera)
Space.CastRay(Vector3 direction, Vector3 origin, CastData data)
Space.CheckCollision(Actor actor, List actorList)
Space.CheckSphereContainsGeometry(Vector3 center, float radius)
Space.GetActor(String actorName)
Space.GetActorList()
Space.SetFogAttributes(boolean enable, int type, Color color, float density, float start, float end)
Space.UpdateScene()
```

## `View` — 8 methods

Properties:

- `cameraNode - CameraNode`
- `iZOrder - int`
- `isVisible - int`
- `screenHeight - int (read only)`
- `screenWidth - int (read only)`

```
View.AddOption(int optionID, String optionName)
View.Delete()
View.DisplayText()
View.EndOptions()
View.SetBounds(int left, int top, int right, int bottom)
View.SetSpace(Space space)
View.StartOptions(Actor targetActor)
View.View(String viewName)
```

## `Vector3` — 7 methods

Properties:

- `length - float (read only)`
- `lengthSquared - float (read only)`
- `unit - Vector3 (read only)`
- `x - float`
- `y - float`
- `z - float`

```
Vector3.Add(Vector3 vector)
Vector3.Copy()
Vector3.Cross(Vector3 vector)
Vector3.Dot(Vector3 vector)
Vector3.Multiply(float scalar)
Vector3.Subtract(Vector3 vector)
Vector3.Vector3(Vector3 vector, float x, float y, float z)
```

## `Joystick` — 5 methods

Properties:

- `iRxAxisValue - int (read only)`
- `iRyAxisValue - int (read only)`
- `iRzAxisValue - int (read only)`
- `iXAxisValue - int (read only)`
- `iYAxisValue - int (read only)`
- `iZAxisValue - int (read only)`

```
Joystick.SetAxisDeadZone(int axis, int value)
Joystick.SetAxisModeToAbs()
Joystick.SetAxisModeToRel()
Joystick.SetAxisRange(int axis, int min, int max, int indexPOV)
Joystick.SetRoute(Actor actor)
```

## `SoundResource` — 5 methods

Properties:

- `ambient - boolean`
- `attenuation - float`
- `cutoffDistance - float`
- `duration - int (read only)`
- `loop - boolean`
- `pan - float`
- `stream - boolean`
- `volume - float`

```
SoundResource.LoadFromFile(String filename)
SoundResource.Play()
SoundResource.Resume()
SoundResource.SoundResource(Actor actor)
SoundResource.Stop()
```

## `ActorNode` — 4 methods

Properties:

- `firstChild - ActorNode (read only)`
- `nextSibling - ActorNode (read only)`
- `nodeName - String (read only)`
- `nodeType - int (read only)`
- `owner - Actor (read only)`
- `parent - ActorNode (read only)`

```
ActorNode.CreateMaterialCopy(int materialIndex)
ActorNode.FlushColModel()
ActorNode.GetBoundingBox(Vector3 min, Vector3 max)
ActorNode.SetMaterial(int materialIndex, Material material)
```

## `KChain` — 4 methods

Properties:

- `maxIterations - int`
- `numLinks - int (read only)`
- `thresholdSqr - float`

```
KChain.MoveTo(Transform goal)
KChain.SetLinkStiffness(int index, float stiffness)
KChain.UpdateActor()
KChain.UpdateChain()
```

## `Keyboard` — 4 methods

Properties:

- `lastKeycode - int (read only)`
- `repeatCount - int (read only)`
- `shiftKeyDown - boolean (read only)`

```
Keyboard.DisableAuthorToolKeys()
Keyboard.EnableAuthorToolKeys()
Keyboard.SetKeyChangeRoute(int keyCode, Actor actorHandle, int pollRate)
Keyboard.SetKeyRoute(int keyCode, Actor actorHandle)
```

## `Mouse` — 4 methods

Properties:

- `lastPickedActor - Actor (read only)`
- `lastPickedFloor - ActorNode (read only)`

```
Mouse.ClearMouseRoute(int mouseCode, Actor actor)
Mouse.DisableToolMouseBehavior()
Mouse.EnableToolMouseBehavior()
Mouse.SetMouseRoute(int mouseCode, Actor actor)
```

## `Skill` — 4 methods

Properties:

- `duration - int (read only)`
- `startTime - int (read only)`

```
Skill.GetFootNodesList(Actor actor)
Skill.GetHandNodesList(Actor actor)
Skill.Sample(Actor actor, String nodeName, int time)
Skill.SampleNode(Actor actor, String nodeName, int time)
```

## `Transform` — 4 methods

Properties:

- `rotation - Rotation3`
- `translation - Vector3`

```
Transform.Copy()
Transform.Inverse()
Transform.Multiply(Transform transform, Vector3 vector)
Transform.Transform(Transform src, Rotation3 rotation, Vector3 translation)
```

## `App` — 3 methods

```
App.OpenURL(String url, String target)
App.Shutdown()
App.UserEvent(String event, String data)
```

## `Console` — 3 methods

```
Console.Clear()
Console.Print()
Console.PrintNl()
```

## `Event` — 3 methods

```
Event.Copy()
Event.Event()
Event.Test()
```

## `Rotation3` — 3 methods

Properties:

- `angle - float`
- `axis - Vector3`

```
Rotation3.Inverse()
Rotation3.Multiply(Rotation3 rotation, Vector3 vector)
Rotation3.Rotation3(Rotation3 src, Vector3 axis, float angle, Vector3 fromVec)
```

## `SoundDriver` — 3 methods

Properties:

- `listener - ActorNode`
- `speedOfSound - float`
- `volume - float`

```
SoundDriver.Resume()
SoundDriver.Start()
SoundDriver.Stop()
```

## `Path2` — 2 methods

Properties:

- `numConfigs - int (read only)`
- `parentNode - ActorNode (read only)`

```
Path2.GetTransform(float index)
Path2.SetTransform(int index, Transform xform)
```

## `Color` — 1 methods

Properties:

- `alpha - float`
- `blue - float`
- `green - float`
- `red - float`

```
Color.Color(float red, float green, float blue, float alpha)
```

## `Filter` — 1 methods

Properties:

- `actor - Actor (read only)`
- `node - ActorNode (read only)`
- `type - int (read only)`

```
Filter.Filter(int type, FilterItem item)
```

## `HFSM` — 1 methods

Properties:

- `state - String (read only)`
- `transition - String (read only)`

```
HFSM.SetChoiceTransition(String transitionName)
```

## `Texture` — 1 methods

Properties:

- `flags - int`
- `name - String`
- `type - int`

```
Texture.Texture(String name, int type, int flags)
```

