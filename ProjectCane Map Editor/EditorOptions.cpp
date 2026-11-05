#include "EditorOptions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <unordered_map>

const char *PropertyName(const std::int16_t eopid)
{
    switch (eopid)
    {
    case 0:
        return "Class ID";
    case 1:
        return "Derived from class";
    case 2:
        return "Remove object";
    case 3:
        return "Suck/hide limits";
    case 4:
        return "Add object";
    case 5:
        return "Clone object";
    case 6:
        return "World";
    case 7:
        return "In world";
    case 8:
        return "Parent ALO";
    case 9:
        return "Set parent";
    case 10:
        return "Is ancestor";
    case 11:
        return "Object ID";
    case 12:
        return "Proxy object ID";
    case 13:
        return "Matches name";
    case 14:
        return "Custom bool property";
    case 15:
        return "Custom integer property";
    case 16:
        return "Custom float property";
    case 17:
        return "Custom curve property";
    case 18:
        return "Custom limits property";
    case 19:
        return "Custom OID property";
    case 20:
        return "Custom vector property";
    case 21:
        return "Initial velocity";
    case 22:
        return "Initial angular velocity";
    case 23:
        return "Position target OID";
    case 24:
        return "Animation local time";
    case 25:
        return "Animation playback rate";
    case 26:
        return "Render priority";
    case 27:
        return "Invalidate lighting";
    case 28:
        return "Frozen";
    case 29:
        return "Local position";
    case 30:
        return "Original position";
    case 31:
        return "World position";
    case 32:
        return "Local orientation";
    case 33:
        return "Original orientation";
    case 34:
        return "World orientation";
    case 35:
        return "Euler rotation";
    case 36:
        return "Local velocity";
    case 37:
        return "Angular velocity";
    case 38:
        return "Linear acceleration";
    case 39:
        return "Angular acceleration";
    case 40:
        return "Root ALO";
    case 41:
        return "Fast-shadow radius";
    case 42:
        return "Fast-shadow depth";
    case 43:
        return "Cast shadow";
    case 44:
        return "Shadow shader OID";
    case 45:
        return "Shadow near radius";
    case 46:
        return "Shadow far radius";
    case 47:
        return "Shadow near cast";
    case 48:
        return "Shadow far cast";
    case 49:
        return "Shadow cone angle";
    case 50:
        return "Shadow frustum up";
    case 51:
        return "Dynamic-shadow object OID";
    case 52:
        return "Do not freeze";
    case 53:
        return "Freeze mode";
    case 54:
        return "Restore position";
    case 55:
        return "Restore-position acknowledgement";
    case 56:
        return "Position spring";
    case 57:
        return "Position spring detail";
    case 58:
        return "Position damping";
    case 59:
        return "Position damping detail";
    case 60:
        return "Restore rotation";
    case 61:
        return "Restore-rotation acknowledgement";
    case 62:
        return "Animation priority";
    case 63:
        return "Rotation spring";
    case 64:
        return "Rotation spring detail";
    case 65:
        return "Rotation damping";
    case 66:
        return "Rotation damping detail";
    case 67:
        return "Position smoothing";
    case 68:
        return "Position smoothing detail";
    case 69:
        return "Rotation smoothing";
    case 70:
        return "Rotation smoothing detail";
    case 71:
        return "Maximum position acceleration";
    case 72:
        return "Maximum rotation acceleration";
    case 73:
        return "Default position acknowledgement";
    case 74:
        return "Default rotation acknowledgement";
    case 75:
        return "Cel-border color";
    case 76:
        return "Look-at rotation mode";
    case 77:
        return "Look-at ignore radius";
    case 78:
        return "Look-at pan function";
    case 79:
        return "Look-at pan limits";
    case 80:
        return "Look-at tilt function";
    case 81:
        return "Look-at tilt limits";
    case 82:
        return "Look-at enabled priority";
    case 83:
        return "Look-at disabled priority";
    case 84:
        return "Look-at disabled priority (OID-encoded)";
    case 85:
        return "ACTLA target";
    case 86:
        return "Resume ACTLA";
    case 87:
        return "Pause ACTLA";
    case 88:
        return "Match rotation to velocity";
    case 89:
        return "Children";
    case 90:
        return "Scrolling master speeds";
    case 91:
        return "Target attack mask";
    case 92:
        return "Target radius";
    case 93:
        return "Target hit test";
    case 94:
        return "Eyes closed";
    case 95:
        return "Cel color";
    case 96:
        return "Override cel color";
    case 97:
        return "Throb kind";
    case 98:
        return "Throb in color";
    case 99:
        return "Throb out color";
    case 100:
        return "Throb transition time";
    case 101:
        return "Sound effect ID";
    case 102:
        return "Sound start distance";
    case 103:
        return "Sound full-volume distance";
    case 104:
        return "Sound volume";
    case 105:
        return "Sound pitch";
    case 106:
        return "Sound repeat limits";
    case 107:
        return "Start sound";
    case 108:
        return "Stop sound";
    case 109:
        return "Sound Doppler";
    case 110:
        return "Cane interaction";
    case 111:
        return "Cane sweep interaction";
    case 112:
        return "Cane rush interaction";
    case 113:
        return "Cane smash interaction";
    case 114:
        return "Bomb interaction";
    case 115:
        return "Shock interaction";
    case 116:
        return "Pose-combo OID";
    case 117:
        return "Fade in";
    case 118:
        return "Fade out";
    case 119:
        return "Use real clock";
    case 120:
        return "Current SMA";
    case 121:
        return "Current ASEGA";
    case 122:
        return "Nearest ASEGA";
    case 123:
        return "Force camera fade";
    case 124:
        return "Match another object";
    case 125:
        return "Hidden";
    case 126:
        return "Set transform basis";
    case 127:
        return "Mass";
    case 128:
        return "Material kind";
    case 129:
        return "Gravity vector";
    case 130:
        return "Disable interaction (presence activates)";
    case 131:
        return "Force constraint";
    case 132:
        return "Torque constraint";
    case 133:
        return "Sphere collision radius";
    case 134:
        return "Buoyancy";
    case 135:
        return "Viscosity";
    case 136:
        return "Ignore locked objects";
    case 137:
        return "Iceable";
    case 138:
        return "No gravity";
    case 139:
        return "No contacts for hierarchy";
    case 140:
        return "No contacts for self";
    case 141:
        return "No center contacts";
    case 142:
        return "Contact list";
    case 143:
        return "Collision zone kind";
    case 144:
        return "Self collision radius";
    case 145:
        return "Camera collision style";
    case 146:
        return "Edge-grab operation";
    case 147:
        return "Point-in-BSP quick query";
    case 148:
        return "Center collision contacts";
    case 247:
        return "Fade duration";
    case 248:
        return "Maximum lifetime";
    case 249:
        return "Break-piece state";
    case 250:
        return "Ignore break pieces";
    case 251:
        return "On animation OID";
    case 252:
        return "Off animation OID";
    case 253:
        return "Push object OID";
    case 254:
        return "Excluded push object OID";
    case 255:
        return "Push class CID";
    case 256:
        return "Excluded push class CID";
    case 257:
        return "Repush delay";
    case 258:
        return "Button type";
    case 259:
        return "Animation speed";
    case 260:
        return "Silent";
    case 261:
        return "Reapply animation";
    case 262:
        return "Manual reset";
    case 263:
        return "Checkpointed";
    case 264:
        return "Button state-machine goal rule";
    case 265:
        return "Dialog OID";
    case 266:
        return "Button state";
    case 267:
        return "On animation OID";
    case 268:
        return "Off animation OID";
    case 269:
        return "Repush delay";
    case 270:
        return "Animation speed";
    case 271:
        return "Silent";
    case 272:
        return "Reapply animation";
    case 273:
        return "Checkpointed";
    case 274:
        return "Must exit volume";
    case 275:
        return "Push object OID";
    case 276:
        return "Volume state-machine goal rule";
    case 277:
        return "Send change events";
    case 278:
        return "Objects within volume (runtime)";
    case 279:
        return "Dialog OID";
    case 280:
        return "Volume-button state";
    case 286:
        return "Pickup attraction range";
    case 287:
        return "Pickup animation OID";
    case 288:
        return "Wait for pickup animation";
    case 289:
        return "Alarm-disabling breakable OID";
    case 290:
        return "Detection sensor OID";
    case 291:
        return "Responding guard OID";
    case 292:
        return "Mute alarm siren";
    case 293:
        return "Automatic reset delay";
    case 294:
        return "Current alarm state (runtime)";
    case 295:
        return "Resolved sensor list (runtime)";
    case 296:
        return "Trigger alarm (runtime action)";
    case 297:
        return "Set alarm state-machine goal (runtime action)";
    case 299:
        return "Allowed object/name OID";
    case 300:
        return "Ignored object/name OID";
    case 301:
        return "Allowed class CID";
    case 302:
        return "Ignored class CID";
    case 303:
        return "Detect every object";
    case 304:
        return "Activation transition duration";
    case 305:
        return "Deactivation transition duration";
    case 306:
        return "Starting sensor state";
    case 307:
        return "Owning alarm (runtime)";
    case 308:
        return "Sensor state (runtime)";
    case 309:
        return "Sensor mode (runtime)";
    case 310:
        return "Remain disabled indefinitely (runtime)";
    case 311:
        return "Enable sensor (runtime action)";
    case 312:
        return "Disable sensor (runtime action)";
    case 313:
        return "Laser collision mode";
    case 314:
        return "Damage-beam shutdown duration";
    case 315:
        return "Extend laser (runtime action)";
    case 316:
        return "Retract laser (runtime action)";
    case 317:
        return "Damage focus duration";
    case 318:
        return "Damage zap duration";
    case 319:
        return "Damage unfocus duration";
    case 320:
        return "Camera-sensor damage state (runtime)";
    case 321:
        return "Sense animation start frame";
    case 322:
        return "Sense animation end frame";
    case 323:
        return "Sense animation duration";
    case 324:
        return "Sense animation playback";
    case 325:
        return "Damage animation start frame";
    case 326:
        return "Damage animation end frame";
    case 327:
        return "Damage animation duration";
    case 328:
        return "Damage animation playback";
    case 329:
        return "Disabled animation start frame";
    case 330:
        return "Disabled animation end frame";
    case 331:
        return "Disabled animation duration";
    case 332:
        return "Disabled animation playback";
    case 333:
        return "Disabling flash frame";
    case 334:
        return "Disabling flash speed";
    case 335:
        return "Remain enabled duration";
    case 336:
        return "Remain disabled duration";
    case 344:
        return "Barrier object OID";
    case 345:
        return "Barrier class CID";
    case 346:
        return "Allow passage";
    case 347:
        return "Barrier warp OID";
    case 622:
        return "Break clue";
    case 623:
        return "Maximum clue frame duration";
    case 624:
        return "Collected (runtime)";
    case 609:
        return "Zap damage kind";
    case 610:
        return "Zap throw object OID";
    case 611:
        return "Add zap throw object (runtime action)";
    case 612:
        return "Remove zap throw object (runtime action)";
    case 613:
        return "Reset zap throw count (runtime action)";
    case 614:
        return "Legacy zap throw height (preserved)";
    case 615:
        return "Cut zap throw velocity";
    case 619:
        return "Checkpoint volume OID";
    case 620:
        return "Checkpoint animation OID";
    case 621:
        return "Checkpoint warp OID";
    case 625:
        return "Conveyor belt speed";
    case 640:
        return "Flatten radius";
    case 641:
        return "Maximum stuck darts";
    case 642:
        return "Maximum jump height";
    case 712:
        return "Detonation radius";
    case 713:
        return "Player detonation radius";
    case 714:
        return "Detonation trigger";
    case 715:
        return "Detonation direction mode";
    case 716:
        return "Touch damage";
    case 717:
        return "Maximum acceleration";
    case 718:
        return "Maximum impact mass";
    case 719:
        return "Impact mass range";
    case 720:
        return "Explosion scale";
    case 721:
        return "Prime bomb (runtime action)";
    case 722:
        return "Detonate bomb (runtime action)";
    case 723:
        return "Explosion effects";
    case 724:
        return "Reclaim after explosion";
    case 725:
        return "Damage kind";
    case 726:
        return "Bomb sound effect ID";
    case 727:
        return "Bomb sound start distance";
    case 728:
        return "Bomb sound full-volume distance";
    case 729:
        return "Bomb sound volume";
    case 730:
        return "Detonation speed";
    case 731:
        return "Use custom collision contact";
    case 752:
        return "Scurry distance";
    case 753:
        return "Scurry speed";
    case 754:
        return "Scurry frame distance";
    case 755:
        return "Flee speed";
    case 756:
        return "Flee frame distance";
    case 757:
        return "Group radius";
    case 758:
        return "Minimum stopped duration";
    case 759:
        return "Leap speed";
    case 760:
        return "Rat state (runtime)";
    case 761:
        return "Coin count";
    case 762:
        return "Suppress difficulty update";
    case 805:
        return "Wander duration range";
    case 806:
        return "Maximum hits";
    case 807:
        return "Hit-up duration";
    case 808:
        return "Hit-down duration";
    case 809:
        return "Bubble response curve";
    case 810:
        return "Dart state";
    case 811:
        return "Dart speed";
    case 812:
        return "Fire cloud radius";
    case 813:
        return "Strike cloud radius";
    case 814:
        return "Fade duration";
    case 815:
        return "Maximum stuck duration";
    case 823:
        return "Missile launch speed";
    case 824:
        return "Missile target (runtime)";
    case 825:
        return "Missile target position (runtime)";
    case 826:
        return "Follow trajectory";
    case 827:
        return "Add ignored object";
    case 828:
        return "Add ignored class";
    case 829:
        return "Maximum missile speed";
    case 830:
        return "Maximum missile acceleration";
    case 831:
        return "Acceleration target velocity (runtime)";
    case 832:
        return "Lifetime override";
    case 833:
        return "Target missile maximum speed";
    case 834:
        return "Trajectory bank ratio";
    case 835:
        return "Target velocity";
    case 887:
        return "Starting jump-volume OID";
    case 888:
        return "Activate JLO";
    case 889:
        return "Deactivate JLO";
    case 890:
        return "Ambient sound enabled";
    case 891:
        return "Ambient sound start distance";
    case 892:
        return "Ambient sound full-volume distance";
    case 893:
        return "Disable automatic spin";
    case 948:
        return "Fly kind";
    case 949:
        return "Initial fly state";
    case 950:
        return "Fly state (runtime)";
    case 951:
        return "Flight cylinder radius range";
    case 952:
        return "Flight cylinder height range";
    case 953:
        return "Horizontal flight speed range";
    case 954:
        return "Vertical flight speed range";
    case 955:
        return "Soaring appearance child OID";
    case 956:
        return "End-of-flight soaring fraction";
    case 957:
        return "Flying duration range";
    case 958:
        return "Flying frame duration";
    case 959:
        return "Ground duration range";
    case 960:
        return "Ground frame duration range";
    case 961:
        return "Ground frame pause range";
    case 962:
        return "Droppings interval range";
    case 963:
        return "Flee distance";
    case 964:
        return "Coin count";
    case 965:
        return "Suppress difficulty update";
    case 1054:
        return "Surface normal";
    case 1055:
        return "Surface depth";
    case 1056:
        return "Alpha range";
    case 1140:
        return "Weak point (runtime)";
    case 1145:
        return "Body radius X";
    case 1146:
        return "Body radius Y";
    case 1147:
        return "Reset transform (runtime action)";
    case 1141:
        return "Clockwerk state";
    case 1142:
        return "Tracking enabled";
    case 1143:
        return "Play damage animation";
    case 1144:
        return "Set damage animation";
    case 1148:
        return "Hits remaining";
    case 1149:
        return "Clockwerk damage state";
    case 1176:
        return "Hits remaining (runtime)";
    case 1177:
        return "Destruction explosion (runtime)";
    case 1178:
        return "Damage decoy";
    case 602:
        return "Camera target OID";
    case 603:
        return "Camera eye position";
    case 604:
        return "Camera view vector";
    case 605:
        return "Camera up vector";
    case 606:
        return "Enable camera";
    case 607:
        return "Disable camera";
    case 894:
        return "Landing marker OID";
    case 895:
        return "Resolved landing marker (runtime)";
    case 896:
        return "Cannon controller OID";
    case 897:
        return "Resolved cannon controller (runtime)";
    case 201:
        return "Enable splashes";
    case 202:
        return "Enable water zap";
    case 203:
        return "Water current";
    case 204:
        return "Add water-zap object OID";
    case 205:
        return "Add water-zap object (runtime)";
    case 206:
        return "Remove water-zap object (runtime)";
    case 207:
        return "Reset throw count (runtime action)";
    case 208:
        return "Water throw height";
    case 209:
        return "Cut throw velocity";
    case 210:
        return "Enable water wake sound";
    case 211:
        return "Enable splash droplets";
    case 212:
        return "Exclude break part OID";
    case 213:
        return "Remain break part OID";
    case 214:
        return "Fixed break part OID";
    case 215:
        return "Remain-fixed break part OID";
    case 216:
        return "Remain after breaking";
    case 217:
        return "Absorb-whack velocity";
    case 218:
        return "High-priority object OID";
    case 219:
        return "Break-position velocity scale";
    case 220:
        return "Linear break speed";
    case 221:
        return "Angular break speed";
    case 222:
        return "Add touch object OID";
    case 223:
        return "Add touch class CID";
    case 224:
        return "Add absorb class CID";
    case 225:
        return "Break on player";
    case 226:
        return "Break on bomb";
    case 227:
        return "Hits required to break";
    case 228:
        return "Break now (runtime action)";
    case 229:
        return "Broken";
    case 230:
        return "Ignore break pieces";
    case 231:
        return "Explosion reach kind";
    case 232:
        return "Explosion scale";
    case 233:
        return "Coin count";
    case 234:
        return "Coin position offset";
    case 235:
        return "Suppress difficulty update";
    case 236:
        return "Break sound ID";
    case 237:
        return "Break sound start distance";
    case 238:
        return "Break sound full-volume distance";
    case 239:
        return "Break sound volume";
    case 240:
        return "Break explosion OID";
    case 241:
        return "Skip break effects";
    case 242:
        return "Impact mass multiplier";
    case 243:
        return "Impact force constraint";
    case 244:
        return "Impact torque constraint";
    case 245:
        return "Impacting object OID";
    case 246:
        return "Contact damage kind";
    case 298:
        return "Owning alarm (runtime)";
    case 153:
        return "Warp velocity";
    case 154:
        return "Warp velocity X";
    case 155:
        return "Warp velocity Y";
    case 156:
        return "Warp velocity Z";
    case 157:
        return "Default warp";
    case 158:
        return "Difficult-mode default warp";
    case 159:
        return "Warp alias OID";
    case 160:
        return "Initial camera angle";
    case 161:
        return "Set state-machine goal";
    case 162:
        return "Trigger warp";
    case 149:
        return "Local position";
    case 150:
        return "Local transform matrix";
    case 151:
        return "World position (runtime)";
    case 152:
        return "World transform matrix (runtime)";
    case 337:
        return "Grab direction";
    case 338:
        return "Jump boost";
    case 339:
        return "Grab sound effect";
    case 340:
        return "Release sound effect";
    case 341:
        return "Hook radius";
    case 342:
        return "Special grab behavior";
    case 343:
        return "Maximum jump-target height";
    case 489:
        return "Explode (runtime)";
    case 490:
        return "Explode with override (runtime)";
    case 421:
        return "Origin type";
    case 422:
        return "Point origin";
    case 423:
        return "Box minimum";
    case 424:
        return "Box maximum";
    case 425:
        return "Reference object OID";
    case 426:
        return "Origin shape OID";
    case 427:
        return "Direction mode";
    case 428:
        return "Direction vector";
    case 429:
        return "Radial direction";
    case 430:
        return "Curve direction";
    case 431:
        return "Surface-offset range";
    case 432:
        return "Velocity coordinate space";
    case 433:
        return "Speed range";
    case 434:
        return "Forward-speed ratio";
    case 435:
        return "Tilt range";
    case 436:
        return "Particles per ring";
    case 437:
        return "Random ring angle";
    case 438:
        return "Skipped-time range";
    case 439:
        return "Add skeleton (authoring action)";
    case 440:
        return "Particle-count range";
    case 441:
        return "Rendered object OID";
    case 442:
        return "Touch object OID";
    case 443:
        return "Next rendered object OID";
    case 444:
        return "Start delay";
    case 445:
        return "Share one particle group";
    case 446:
        return "Acceleration";
    case 447:
        return "Velocity damping";
    case 448:
        return "Curl amount";
    case 449:
        return "Curl normal";
    case 450:
        return "Curl coordinate space";
    case 451:
        return "Particle lifetime";
    case 452:
        return "Birth-time range";
    case 453:
        return "Alpha curve";
    case 454:
        return "Particle representation";
    case 455:
        return "RIP type";
    case 456:
        return "Trail RIP type";
    case 457:
        return "RIP scale range";
    case 458:
        return "RIP scale curve";
    case 459:
        return "RIP group behavior";
    case 460:
        return "Expansion speed";
    case 461:
        return "Particle shader OID";
    case 462:
        return "Scale curve";
    case 463:
        return "Texture-frame curve";
    case 464:
        return "Color curve";
    case 465:
        return "Span shader frames";
    case 466:
        return "Shader-loop duration";
    case 467:
        return "Random initial shader frame";
    case 468:
        return "Motion model";
    case 469:
        return "Spline-target OID";
    case 470:
        return "Movement behavior";
    case 471:
        return "Flying turn rate";
    case 472:
        return "Rolling mirror behavior";
    case 473:
        return "Angular-speed range";
    case 474:
        return "Random initial roll";
    case 475:
        return "Bullets cause damage";
    case 476:
        return "Bullets ricochet";
    case 477:
        return "Sound effect ID";
    case 478:
        return "Sound start distance";
    case 479:
        return "Sound full-volume distance";
    case 480:
        return "Sound volume";
    case 481:
        return "Explode (runtime)";
    case 482:
        return "Explode with override (runtime)";
    case 483:
        return "Secondary explosion OID";
    case 484:
        return "Child-emission rate";
    case 485:
        return "Shrapnel radius";
    case 486:
        return "Shrapnel elasticity";
    case 487:
        return "Shrapnel friction";
    case 488:
        return "Damage enabled";
    case 176:
        return "Point position";
    case 177:
        return "World point position (runtime)";
    case 626:
        return "Hide flatten radius";
    case 627:
        return "Hide movement kind";
    case 628:
        return "Tunnel hide point";
    case 629:
        return "Hide camera-node OID";
    case 630:
        return "Enable hide detection";
    case 631:
        return "Maximum jump-target height";
    case 632:
        return "Alternate hide behavior";
    case 836:
        return "Jump marker radius";
    case 837:
        return "Ignore-object OID";
    case 1030:
        return "Maximum height above spire marker";
    case 1031:
        return "Exclude spire from jump-target history";
    case 1103:
        return "Point inside volume (runtime query)";
    case 616:
        return "Target categories";
    case 617:
        return "Target radius";
    case 618:
        return "Enable hit testing";
    case 633:
        return "Hide movement kind";
    case 634:
        return "Use hiding camera tunnel";
    case 635:
        return "Hide camera-node OID";
    case 636:
        return "Endpoint peek flags";
    case 637:
        return "Suppress guard detection while hiding";
    case 638:
        return "Maximum acquisition height";
    case 639:
        return "Block flatten target query";
    case 643:
        return "Pipe facing direction";
    case 644:
        return "Pipe grab sound";
    case 645:
        return "Pipe release sound";
    case 646:
        return "Initial pipe orientation";
    case 647:
        return "Pipe runtime action";
    case 648:
        return "Block targeting while attached";
    case 649:
        return "Optional rope-object OID";
    case 650:
        return "Runtime control-shape reference";
    case 651:
        return "Maximum pipe jump-target height";
    case 1032:
        return "Slippery rail";
    case 1033:
        return "Maximum rail speed";
    case 1034:
        return "Rail gravity scale";
    case 1035:
        return "Maximum rail jump-target height";
    case 1036:
        return "Suppress rail jump-target history";
    case 1037:
        return "Rail control-direction curve OID";
    case 1038:
        return "Rail runtime control-curve reference";
    case 163:
        return "Exit warp OID";
    case 164:
        return "Alternate exit direction";
    case 165:
        return "Alternate warp OID";
    case 166:
        return "Wipe type";
    case 167:
        return "Unblock delay";
    case 168:
        return "Default exit";
    case 169:
        return "Keyed exit";
    case 170:
        return "Follow default exit";
    case 171:
        return "Use default exit";
    case 172:
        return "Show totals";
    case 173:
        return "Trigger-wipe delay";
    case 174:
        return "Trigger exit";
    case 175:
        return "Exit state";
    case 281:
        return "Playable";
    case 282:
        return "Make active";
    case 283:
        return "Invulnerable";
    case 284:
        return "Current playable state";
    case 709:
        return "Base contact offset";
    case 710:
        return "Turning speed ratio";
    case 711:
        return "Gravity ratio";
    case 732:
        return "Throw held object";
    case 733:
        return "Unhook";
    case 734:
        return "Profile action";
    case 735:
        return "Render cane trail";
    case 736:
        return "Pipe travel direction";
    case 737:
        return "Place on pipe";
    case 738:
        return "Set puppet";
    case 739:
        return "Set stunned";
    case 740:
        return "Deploy mine";
    case 741:
        return "Cancel peek";
    case 742:
        return "Disable STEP custom contacts";
    case 743:
        return "Trigger lock-on";
    case 744:
        return "Trigger displacement";
    case 745:
        return "Trigger secondary displacement";
    case 491:
        return "Light type";
    case 492:
        return "Highlight HSV";
    case 493:
        return "Midtone strength";
    case 494:
        return "Shadow strength";
    case 495:
        return "Highlight angle";
    case 496:
        return "Midtone angle";
    case 497:
        return "Shadow angle";
    case 498:
        return "Light direction";
    case 499:
        return "Dynamic";
    case 500:
        return "Dynamic alternate";
    case 501:
        return "Exclude dynamic objects";
    case 502:
        return "Falloff near/far";
    case 503:
        return "Cone angle";
    case 504:
        return "Hotspot angle";
    case 505:
        return "Light up vector";
    case 506:
        return "Legacy diffuse strength (unused)";
    case 507:
        return "Legacy saturate color (unused)";
    case 508:
        return "Legacy light flag (reserved)";
    case 509:
        return "Legacy light value 0 (reserved)";
    case 510:
        return "Legacy light value 1 (reserved)";
    case 511:
        return "Legacy light value 2 (reserved)";
    case 512:
        return "Legacy light value 3 (reserved)";
    case 513:
        return "Default state machine";
    case 514:
        return "Apply state machine (runtime action)";
    case 516:
        return "Goal state OID";
    case 517:
        return "Current state OID";
    case 518:
        return "Next state OID";
    case 519:
        return "Local state time";
    case 520:
        return "Current animation segment";
    case 521:
        return "Seek state (runtime action)";
    case 522:
        return "Retract state (runtime action)";
    case 523:
        return "Owning exit";
    case 608:
        return "Follow camera Z";
    case 979:
        return "Initial-state OID";
    case 980:
        return "Initial skip delay";
    case 981:
        return "Prize state";
    case 982:
        return "Never reuse";
    case 983:
        return "Attraction effect rate";
    case 984:
        return "Collectible";
    case 985:
        return "Maximum lifetime range";
    case 986:
        return "Reuse delay range";
    case 987:
        return "Start timed challenge (runtime)";
    case 763:
        return "Tail spring strength";
    case 764:
        return "Tail world damping";
    case 765:
        return "Tail local damping";
    case 766:
        return "Tail maximum bend angle";
    case 767:
        return "Tail solver iterations";
    case 768:
        return "Tail gravity";
    case 769:
        return "Tail rotation unlocked";
    case 770:
        return "Tail tip position";
    case 771:
        return "Collectible OID";
    case 772:
        return "Harvester OID";
    case 773:
        return "Vehicle OID";
    case 774:
        return "Prize OID";
    case 775:
        return "Spawn tunnel OID";
    case 776:
        return "Active ROC maximum (easy)";
    case 777:
        return "Active ROC maximum (hard)";
    case 778:
        return "Active ROC count curve";
    case 779:
        return "Active ROH maximum (easy)";
    case 780:
        return "Active ROH maximum (hard)";
    case 781:
        return "Active ROH count curve";
    case 782:
        return "ROC spawn interval limits (easy)";
    case 783:
        return "ROC spawn interval limits (hard)";
    case 784:
        return "ROC spawn interval curve";
    case 785:
        return "ROH spawn interval limits (easy)";
    case 786:
        return "ROH spawn interval limits (hard)";
    case 787:
        return "ROH spawn interval curve";
    case 788:
        return "ROH speed maximum (easy)";
    case 789:
        return "ROH speed maximum (hard)";
    case 790:
        return "ROH speed curve";
    case 791:
        return "ROH turn-rate maximum (easy)";
    case 792:
        return "ROH turn-rate maximum (hard)";
    case 793:
        return "ROH turn-rate curve";
    case 794:
        return "Arena center";
    case 795:
        return "Tube arena radius";
    case 796:
        return "ROC spawn arena radius";
    case 797:
        return "Wander arena radius";
    case 798:
        return "Arena aspect ratio";
    case 799:
        return "Collectible drop height";
    case 800:
        return "Maximum created ROCs";
    case 801:
        return "Encounter variant";
    case 802:
        return "Difficulty tuning set";
    case 803:
        return "ROB tuning curve";
    case 838:
        return "Waypoint pause time";
    case 839:
        return "Synchronization key OID";
    case 840:
        return "Waypoint state-machine goal route";
    case 851:
        return "Camera distance";
    case 852:
        return "Camera distance curve 1";
    case 853:
        return "Camera distance limits 1";
    case 854:
        return "Camera distance curve 2";
    case 855:
        return "Camera distance limits 2";
    case 856:
        return "Camera distance curve 3";
    case 857:
        return "Camera distance limits 3";
    case 858:
        return "Slack angle";
    case 859:
        return "Preferred screen X";
    case 860:
        return "Preferred screen Y";
    case 861:
        return "Manual adjustment weight";
    case 862:
        return "Follow weight";
    case 863:
        return "Lock-on weight";
    case 864:
        return "Focus ratio";
    case 865:
        return "Focus vector";
    case 866:
        return "Cylindrical camera";
    case 867:
        return "Camera behavior flags";
    case 868:
        return "Pan on entry";
    case 869:
        return "Pan while idle";
    case 870:
        return "Camera priority";
    case 871:
        return "Switch while airborne";
    case 872:
        return "Camera framing orientation";
    case 873:
        return "Entry-pan duration";
    case 874:
        return "Entry-pan speed";
    case 875:
        return "Idle-pan duration";
    case 876:
        return "Idle-pan speed";
    case 877:
        return "Cut on activation";
    case 878:
        return "Field-of-view override";
    case 879:
        return "Disable camera squish";
    case 880:
        return "Follow-target OID";
    case 881:
        return "Follow-target curve";
    case 882:
        return "Follow-target limits";
    case 883:
        return "Camera state";
    case 884:
        return "Camera state override";
    case 885:
        return "Camera spring strength";
    case 886:
        return "Camera spring damping";
    case 898:
        return "Launcher aim kind";
    case 899:
        return "Fire interval";
    case 900:
        return "Missile interval";
    case 901:
        return "Launcher mode";
    case 902:
        return "Acceleration time";
    case 903:
        return "Missile speed";
    case 904:
        return "Maximum tilt angle";
    case 905:
        return "Launch speed";
    case 906:
        return "Target speed";
    case 1000:
        return "Lock OID";
    case 1001:
        return "Trigger lock group (runtime)";
    case 1002:
        return "World-state flag index";
    case 1104:
        return "Position within flash (runtime query)";
    case 966:
        return "Dialog state";
    case 967:
        return "Default anchor-point OID";
    case 968:
        return "Default dialog";
    case 969:
        return "Crucial dialog";
    case 970:
        return "Prevent skipping";
    case 971:
        return "Instruction presentation mode";
    case 972:
        return "Confrontation presentation mode";
    case 973:
        return "Add dialog equivalence";
    case 974:
        return "Trigger dialog (runtime)";
    case 975:
        return "Untrigger dialog (runtime)";
    case 976:
        return "Played-state persistence key OID";
    case 977:
        return "Dialog played state (runtime)";
    case 978:
        return "Use confrontation audio";
    case 988:
        return "Subtitle color";
    case 989:
        return "Speaker field of view";
    case 990:
        return "Speaker camera distance";
    case 991:
        return "TV screen position X";
    case 992:
        return "TV screen position Y";
    case 993:
        return "Dialog light offset";
    case 994:
        return "Confrontation light offset";
    case 995:
        return "Confrontation screen position X";
    case 996:
        return "Confrontation screen position Y";
    case 997:
        return "Idle state-machine OID";
    case 998:
        return "Confrontation camera offset";
    case 999:
        return "Confrontation camera roll";
    case 1057:
        return "Kill goal";
    case 1058:
        return "Maximum game time";
    case 1059:
        return "Spawn-slot OID";
    case 1060:
        return "Active enemy minimum";
    case 1061:
        return "Active enemy maximum";
    case 1062:
        return "Progress-to-active-count curve";
    case 1063:
        return "Respawn-enable threshold";
    case 1064:
        return "Initial spawn delay";
    case 1065:
        return "Final spawn delay";
    case 1066:
        return "Progress-to-spawn-delay curve";
    case 1067:
        return "Initial respawn delay";
    case 1068:
        return "Final respawn delay";
    case 1069:
        return "Progress-to-respawn-delay curve";
    case 1070:
        return "Initial difficulty";
    case 1071:
        return "Final difficulty";
    case 1072:
        return "Progress-to-difficulty curve";
    case 1073:
        return "Initial secondary difficulty";
    case 1074:
        return "Final secondary difficulty";
    case 1075:
        return "Progress-to-secondary-difficulty curve";
    case 1076:
        return "Difficulty profile index";
    case 1077:
        return "Difficulty-selection curve";
    case 1078:
        return "Spawner state machine (runtime)";
    case 524:
        return "Music enabled";
    case 525:
        return "Music volume";
    case 526:
        return "Default midtone illumination";
    case 527:
        return "Default shadow illumination";
    case 528:
        return "Sky color";
    case 529:
        return "Camera field of view";
    case 530:
        return "Camera near clip";
    case 531:
        return "Camera far clip";
    case 532:
        return "Fog near distance";
    case 533:
        return "Fog far distance";
    case 534:
        return "Maximum fog";
    case 535:
        return "Fog color";
    case 536:
        return "Camera MRD ratio";
    case 537:
        return "Gold enable time";
    case 538:
        return "Commentary audio interval";
    case 539:
        return "Menu excitement";
    case 540:
        return "Screen darkening";
    case 541:
        return "Smooth screen darkening";
    case 542:
        return "Rich coin minimum";
    case 543:
        return "Rich coin maximum";
    case 544:
        return "Poor coin minimum";
    case 545:
        return "Poor coin maximum";
    case 546:
        return "Spawn random coins";
    case 547:
        return "Player difficulty / suck";
    case 548:
        return "Raw difficulty / suck";
    case 549:
        return "Intermittent sound effect";
    case 550:
        return "Intermittent volume range";
    case 551:
        return "Intermittent repeat range";
    case 552:
        return "Default reverb";
    case 553:
        return "Push reverb";
    case 554:
        return "Pop reverb";
    case 555:
        return "Level visited";
    case 556:
        return "Primary level complete";
    case 557:
        return "Secondary level complete";
    case 558:
        return "Tertiary level complete";
    case 559:
        return "Level key collected";
    case 560:
        return "Secondary task available";
    case 561:
        return "Commentary unlocked for level";
    case 562:
        return "Set level-data value";
    case 563:
        return "Get level-data value";
    case 564:
        return "World visited";
    case 565:
        return "World complete";
    case 566:
        return "Recalculate completion percentage";
    case 567:
        return "Commentary audio playing";
    case 568:
        return "Cancel dialog";
    case 569:
        return "Set SMA prompt";
    case 570:
        return "Acquire letterbox";
    case 571:
        return "Release letterbox";
    case 572:
        return "Dynamic prizes enabled";
    case 573:
        return "Set excitement";
    case 574:
        return "Unset excitement";
    case 575:
        return "Set music register";
    case 576:
        return "Response text";
    case 577:
        return "Refresh MIDI register";
    case 578:
        return "Hands off";
    case 579:
        return "Hands on";
    case 580:
        return "Lock vault";
    case 581:
        return "Unlock vault";
    case 582:
        return "Charm count";
    case 583:
        return "Life count";
    case 584:
        return "CM position";
    case 585:
        return "CM matrix";
    case 586:
        return "Field of view (radians)";
    case 587:
        return "Target field of view (radians)";
    case 588:
        return "FOV smoothing";
    case 589:
        return "Near clip";
    case 590:
        return "Far clip";
    case 591:
        return "Near fog distance";
    case 592:
        return "Far fog distance";
    case 593:
        return "Maximum fog density";
    case 594:
        return "Maximum render-distance ratio";
    case 595:
        return "Push look policy";
    case 596:
        return "Pop look policy";
    case 597:
        return "Current look policy";
    case 598:
        return "Set sniper focus";
    case 599:
        return "Trigger camera jolt";
    case 600:
        return "Cut camera";
    case 601:
        return "Test visible sphere";
    case 1003:
        return "Add circle warp";
    case 1004:
        return "Add bend warp";
    case 1005:
        return "Circle direction";
    case 1006:
        return "Circle wavelength";
    case 1007:
        return "Circle frequency";
    case 1008:
        return "Circle phase";
    case 1009:
        return "Circle position amplitude A";
    case 1010:
        return "Circle position amplitude B";
    case 1011:
        return "Circle UV amplitude A";
    case 1012:
        return "Circle UV amplitude B";
    case 1013:
        return "Bend axis";
    case 1014:
        return "Bend angle range";
    case 1015:
        return "Bend bias";
    case 1016:
        return "Add bend noise";
    case 1017:
        return "Swivel axis";
    case 1018:
        return "Swivel angle range";
    case 1019:
        return "Swivel bias";
    case 1020:
        return "Add swivel noise";
    case 1021:
        return "Warp operator count";
    case 1022:
        return "Current warp operator";
    case 178:
        return "Animation repeat mode";
    case 179:
        return "Default animation segment";
    case 180:
        return "Hands-off during animation";
    case 181:
        return "Animation priority";
    case 182:
        return "Master playback speed";
    case 183:
        return "Difficulty-to-speed curve";
    case 184:
        return "Difficulty-to-speed limits";
    case 185:
        return "Apply animation segment";
    case 186:
        return "Ensure animation segment";
    case 187:
        return "Find animation label";
    case 188:
        return "Animation duration";
    case 189:
        return "Use real-time clock";
    case 190:
        return "Source animation segment";
    case 191:
        return "Animation root object";
    case 192:
        return "Current animation time";
    case 193:
        return "Playback speed";
    case 194:
        return "Master playback speed";
    case 195:
        return "Animation priority";
    case 196:
        return "Hands-off during animation";
    case 197:
        return "Seek animation";
    case 198:
        return "Retract animation";
    case 199:
        return "Snap animation";
    case 200:
        return "Retract active segment";
    case 285:
        return "Add frozen object OID";
    case 515:
        return "Animation root object";
    case 694:
        return "Add guard OID";
    case 695:
        return "Add search-transform OID";
    case 746:
        return "Reach mode";
    case 747:
        return "Host object OID";
    case 748:
        return "Touch object OID";
    case 749:
        return "Allowed attack kinds";
    case 750:
        return "Set natural reach coefficients";
    case 751:
        return "Set centered reach coefficients";
    case 907:
        return "Add ammunition cache";
    case 908:
        return "Share ammunition between clones";
    case 909:
        return "Cache selection mode";
    case 910:
        return "Current ammunition cache";
    case 911:
        return "Enable ammunition cache";
    case 912:
        return "Disable ammunition cache";
    case 913:
        return "Resize ammunition cache";
    case 914:
        return "Reload weapon";
    case 915:
        return "Fire weapon";
    case 916:
        return "Current ammunition object";
    case 917:
        return "Clear firing source";
    case 918:
        return "Firing source OID";
    case 919:
        return "Firing source object";
    case 920:
        return "Firing source point";
    case 921:
        return "Firing source transform";
    case 922:
        return "Firing position";
    case 923:
        return "Firing orientation";
    case 924:
        return "Firing linear velocity";
    case 925:
        return "Firing angular velocity";
    case 926:
        return "Random angular-speed range";
    case 927:
        return "Bomb detonation delay (> 0 enables)";
    case 928:
        return "Clear target information";
    case 929:
        return "Target OID";
    case 930:
        return "Target object";
    case 931:
        return "Target marker";
    case 932:
        return "Target position";
    case 933:
        return "Targeting mode";
    case 934:
        return "Distance-to-prediction-time curve";
    case 935:
        return "Prediction curve alias (same runtime field)";
    case 936:
        return "Prediction-time limits";
    case 937:
        return "Target-distance curve";
    case 938:
        return "Target-distance limits";
    case 939:
        return "Clear aiming constraints";
    case 940:
        return "Aim-control flags";
    case 941:
        return "Aim transform OID";
    case 942:
        return "Aim transform";
    case 943:
        return "Fixed aim tilt (radians)";
    case 944:
        return "Fixed aim pan (radians)";
    case 945:
        return "Explicit launch speed";
    case 946:
        return "Firing-context ALO OID";
    case 947:
        return "Firing context object";
    case 1108:
        return "Choose random path-zone point";
    case 1115:
        return "Clear gameplay script";
    case 1116:
        return "Apply gameplay script";
    case 1117:
        return "Retract gameplay script";
    case 1118:
        return "Add context command";
    case 1119:
        return "Add cut command";
    case 1120:
        return "Add focus command";
    case 1121:
        return "Add label command";
    case 1122:
        return "Add tunnel command";
    case 1123:
        return "Add pause command";
    case 1124:
        return "Add jump command";
    case 1125:
        return "Add run command";
    case 1126:
        return "Add climb command";
    case 1127:
        return "Add attack command";
    case 1128:
        return "Add animation command";
    case 1129:
        return "Add clock command";
    case 1130:
        return "Hands-off during script";
    case 1131:
        return "Add hide command";
    case 1161:
        return "Add rhythm match";
    case 1162:
        return "Add rhythm splice event";
    case 1163:
        return "Add enable-input command";
    case 1164:
        return "Add disable-input command";
    case 1165:
        return "Add enable-effect command";
    case 1166:
        return "Add disable-effect command";
    case 652:
        return "Path zone OID";
    case 653:
        return "Attack distance limits";
    case 654:
        return "Attack timing limits";
    case 655:
        return "Maximum attack angle (degrees)";
    case 656:
        return "Use guard animation";
    case 657:
        return "Use guard animation immediately";
    case 658:
        return "Get immediate guard animation";
    case 659:
        return "Dying fade duration";
    case 660:
        return "Match animation phase";
    case 661:
        return "Add attack effect";
    case 662:
        return "Use death animation";
    case 663:
        return "Set guard state externally";
    case 664:
        return "Use state physics object";
    case 665:
        return "Stun count";
    case 666:
        return "Tumble when stunned";
    case 667:
        return "Jump";
    case 668:
        return "Freefall duration limits";
    case 669:
        return "Lock look direction";
    case 670:
        return "External enemy OID";
    case 671:
        return "Enemy object OID";
    case 672:
        return "Rebind enemy";
    case 673:
        return "Flying guard";
    case 674:
        return "Flying height offset";
    case 675:
        return "Pursuit distance curve";
    case 676:
        return "Patrol route selection";
    case 677:
        return "Set patrol animation";
    case 678:
        return "Coin count";
    case 679:
        return "Local break position";
    case 680:
        return "Disable difficulty scaling";
    case 681:
        return "Explosion OID";
    case 682:
        return "RWM OID";
    case 683:
        return "Idle animation start phase";
    case 684:
        return "Fling multiplier";
    case 685:
        return "Auto-fade while dying";
    case 686:
        return "Current guard state";
    case 687:
        return "Bypass effect-hit suppression";
    case 688:
        return "On final path segment";
    case 689:
        return "Taunt duration limits";
    case 690:
        return "Face incoming damage";
    case 691:
        return "Contact friction";
    case 692:
        return "Absorb damage";
    case 693:
        return "Align to ground";
    case 804:
        return "ROV maximum speed";
    case 816:
        return "Dart template OID";
    case 817:
        return "Dart count";
    case 818:
        return "Fire interval";
    case 819:
        return "Target height offset";
    case 820:
        return "Add rat target OID";
    case 821:
        return "Start target-area change (runtime action)";
    case 822:
        return "Add target-area target (runtime action)";
    case 696:
        return "Near detection radius";
    case 697:
        return "Near detection height";
    case 698:
        return "Sneak radius";
    case 699:
        return "Sneak height";
    case 700:
        return "Sneak-volume OID";
    case 701:
        return "Flashlight child OID/name";
    case 702:
        return "Resolved flashlight (runtime)";
    case 703:
        return "Per-state flashlight target";
    case 704:
        return "Always detect a valid enemy";
    case 705:
        return "Disable enemy detection";
    case 706:
        return "Detection rules";
    case 707:
        return "Flashlight animation-override target OID";
    case 708:
        return "Detection barrier OID";
    case 841:
        return "Maximum pursuit distance";
    case 842:
        return "Maximum vertical pursuit difference";
    case 843:
        return "Pursuit-boundary volume OID";
    case 844:
        return "Resolved pursuit-boundary volume (runtime)";
    case 845:
        return "Near/far detection distance";
    case 846:
        return "Far/near detection cone angle (radians)";
    case 847:
        return "Automatic-detection volume OID";
    case 848:
        return "Resolved automatic-detection volume (runtime)";
    case 849:
        return "Hold player detection active";
    case 850:
        return "Force guard to abandon pursuit";
    case 1023:
        return "Mission forces pursuit abandonment";
    case 1024:
        return "Resolved Mugshot state map (runtime)";
    case 1025:
        return "Return-to-patrol XFM OID";
    case 1026:
        return "Advance Mugshot attack phase (runtime)";
    case 1027:
        return "Current attack blend (runtime)";
    case 1028:
        return "Tank state request (post-load overridden)";
    case 1029:
        return "Allow player eject";
    case 1039:
        return "Turret state machine (runtime)";
    case 1040:
        return "Drive scene camera matrix";
    case 1041:
        return "Camera look mode";
    case 1042:
        return "Track target";
    case 1043:
        return "Turret target OID";
    case 1044:
        return "Tilt limits";
    case 1045:
        return "Pan limits";
    case 1046:
        return "Vault combination";
    case 1047:
        return "Goad dialog entry";
    case 1048:
        return "Goad volume button OID";
    case 1049:
        return "Combination dialog OID";
    case 1050:
        return "Start goading";
    case 1051:
        return "Game time limit";
    case 1052:
        return "Shoot state machine (runtime)";
    case 1053:
        return "Puffer state machine (runtime)";
    case 1079:
        return "Crusher kill-progress contribution";
    case 1080:
        return "Front wheel radius";
    case 1081:
        return "Rear wheel radius";
    case 1082:
        return "Maximum speed";
    case 1083:
        return "Maximum vertical travel";
    case 1084:
        return "Resolved track shape (runtime)";
    case 1085:
        return "Resolved guide-line shape (runtime)";
    case 1086:
        return "Maximum laps";
    case 1087:
        return "Add checkpoint (runtime action)";
    case 1088:
        return "Add vehicle feature (runtime action)";
    case 1089:
        return "Resolved path zone (runtime)";
    case 1090:
        return "SUV state";
    case 1091:
        return "Reset SUV (runtime action)";
    case 1092:
        return "SUV game kind";
    case 1093:
        return "Race lap count";
    case 1094:
        return "Resolved dirt explosion (runtime)";
    case 1095:
        return "Resolved dust explosion (runtime)";
    case 1096:
        return "Resolved boost emitter (runtime)";
    case 1097:
        return "Contact friction";
    case 1098:
        return "Boost count";
    case 1099:
        return "Ahead reservation curve";
    case 1100:
        return "Behind reservation limits";
    case 1101:
        return "Random reservation curve";
    case 1102:
        return "Boost reservation limits";
    case 1105:
        return "Register detected STEPGUARD (runtime)";
    case 1106:
        return "Resolved pursuit-goal XFM (runtime)";
    case 1107:
        return "Resolved patrol-goal XFM (runtime)";
    case 1109:
        return "Impact fast speed";
    case 1110:
        return "Impact slow speed";
    case 1111:
        return "Impact fast duration";
    case 1112:
        return "Maximum velocity delta";
    case 1113:
        return "Set swoop shape (runtime action)";
    case 1114:
        return "LGN state (runtime)";
    case 1150:
        return "Rhythm frames per beat";
    case 1151:
        return "Rhythm beats per measure";
    case 1152:
        return "Queue rhythm sequence (runtime)";
    case 1153:
        return "Clear rhythm sequence (runtime)";
    case 1154:
        return "Ruby state machine (runtime)";
    case 1155:
        return "Boss movement-goal XFM OID";
    case 1156:
        return "Add timed ASEGA rhythm event (runtime)";
    case 1157:
        return "Remove timed ASEGA rhythm event (runtime)";
    case 1158:
        return "Damage Ruby boss (runtime)";
    case 1159:
        return "Resolved rhythm start transform (runtime)";
    case 1160:
        return "Resolved rhythm finish controller (runtime)";
    case 1167:
        return "Absorbed-hit count (runtime)";
    case 1168:
        return "Bind three hit-reaction ASEGs (runtime)";
    case 1169:
        return "Player detection enabled (runtime)";
    case 1170:
        return "MGV state (runtime)";
    case 1171:
        return "Apply death throw (runtime action)";
    case 1172:
        return "Security code count";
    case 1173:
        return "Lives";
    case 1174:
        return "Add respawn point (runtime action)";
    case 1175:
        return "Respawn object (runtime action)";
    default:
        return nullptr;
    }
}

namespace
{
std::string SafePropertyLabel(const std::int16_t eopid)
{
    if (const char *name = PropertyName(eopid); name != nullptr && name[0] != '\0')
        return name;
    return "EOPID " + std::to_string(eopid);
}

std::string SafeClassLabel(const std::int16_t cid)
{
    return BrxDocument::EditorClassName(cid);
}
}

const char *PropertyNameForCid(const std::int16_t cid, const std::int16_t eopid)
{
    if (cid != 80)
        return PropertyName(eopid);
    switch (eopid)
    {
    case 348:
        return "Particle count";
    case 349:
        return "Auto pause";
    case 350:
        return "Emission mode";
    case 351:
        return "Particle rate range";
    case 352:
        return "Constant particle count";
    case 353:
        return "Pause probability";
    case 354:
        return "Pause duration range";
    case 355:
        return "Enabled";
    case 356:
        return "Count is density";
    case 357:
        return "Reference object OID";
    case 358:
        return "Render object OID";
    case 359:
        return "Touch object OID";
    case 360:
        return "Next render object OID";
    case 361:
        return "Emitter group OID";
    case 362:
        return "Pause for duration";
    case 363:
        return "Pause forever";
    case 364:
        return "Unpause";
    case 365:
        return "Paused";
    case 366:
        return "Origin type";
    case 367:
        return "Origin position";
    case 368:
        return "Box minimum";
    case 369:
        return "Box maximum";
    case 370:
        return "Origin shape OID";
    case 371:
        return "Direction type";
    case 372:
        return "Direction vector";
    case 373:
        return "Radial direction";
    case 374:
        return "Curve direction";
    case 375:
        return "Surface offset range";
    case 376:
        return "Velocity space";
    case 377:
        return "Speed range";
    case 378:
        return "Forward speed ratio";
    case 379:
        return "Acceleration";
    case 380:
        return "Velocity damping";
    case 381:
        return "Curl amount";
    case 382:
        return "Curl normal";
    case 383:
        return "Curl space";
    case 384:
        return "Tilt range";
    case 385:
        return "Particles per ring";
    case 386:
        return "Random ring angle";
    case 387:
        return "Particle lifetime";
    case 388:
        return "Skip-time range";
    case 389:
        return "Birth-time range";
    case 390:
        return "Alpha curve";
    case 391:
        return "Particle representation";
    case 392:
        return "RIP type";
    case 393:
        return "Trail RIP type";
    case 394:
        return "RIP scale range";
    case 395:
        return "RIP scale curve";
    case 396:
        return "RIP group type";
    case 397:
        return "Expansion speed";
    case 398:
        return "Particle shader OID";
    case 399:
        return "BLIP scale curve";
    case 400:
        return "Texture-frame curve";
    case 401:
        return "Color curve";
    case 402:
        return "Span shader frames";
    case 403:
        return "Shader loop duration";
    case 404:
        return "Random shader frame";
    case 405:
        return "BLIP motion type";
    case 406:
        return "Spline target OID";
    case 407:
        return "BLIP movement type";
    case 408:
        return "Flying turn rate";
    case 409:
        return "Rolling mirror mode";
    case 410:
        return "Angular-speed range";
    case 411:
        return "Random roll";
    case 412:
        return "Add skeleton source";
    case 413:
        return "Bullet causes damage";
    case 414:
        return "Bullet ricochets";
    case 415:
        return "Explosion OID";
    case 416:
        return "Child emission rate";
    case 417:
        return "Shrapnel radius";
    case 418:
        return "Shrapnel elasticity";
    case 419:
        return "Shrapnel friction";
    case 420:
        return "Damage enabled";
    default:
        return PropertyName(eopid);
    }
}

const std::array<EditorOptionDefinition, 1179> &EditorOptionDefinitions()
{
    static const auto definitions = []
    {
        std::array<EditorOptionDefinition, 1179> result{};
        enum class EditorGeneratedOptionKind : std::uint8_t
        {
            Bool,
            Float,
            Vector3,
            Vector4,
            Int32,
            Float2,
            UInt32,
            Int16,
            TypedInt16
        };
        const auto convert = [](const EditorGeneratedOptionKind kind)
        {
            switch (kind)
            {
            case EditorGeneratedOptionKind::Bool:
                return BrxPropertyKind::Bool;
            case EditorGeneratedOptionKind::Float:
                return BrxPropertyKind::Float;
            case EditorGeneratedOptionKind::Vector3:
                return BrxPropertyKind::Vector3;
            case EditorGeneratedOptionKind::Vector4:
                return BrxPropertyKind::Vector4;
            case EditorGeneratedOptionKind::Int32:
                return BrxPropertyKind::Int32;
            case EditorGeneratedOptionKind::Float2:
                return BrxPropertyKind::Float2;
            case EditorGeneratedOptionKind::UInt32:
                return BrxPropertyKind::UInt32;
            case EditorGeneratedOptionKind::TypedInt16:
                return BrxPropertyKind::TypedInt16;
            default:
                return BrxPropertyKind::Int16;
            }
        };
#define OptionKind EditorGeneratedOptionKind
#define BRX_OPTION(id, kind, optionFlags, argc) \
        result.at(id) = EditorOptionDefinition{ \
            (static_cast<std::uint32_t>(optionFlags) & 0x400u) != 0u \
                ? BrxPropertyKind::Dynamic : convert(kind), \
            static_cast<std::uint32_t>(optionFlags), \
            static_cast<std::uint16_t>(argc), true};
#include "OptionSchema.generated.inc"
#undef BRX_OPTION
#undef OptionKind
#define BRX_OPTION_DEFAULT(id, bits) result.at(id).runtimeDefaultable = true; result.at(id).runtimeDefaultBits = (bits);
#include "OptionDefaults.generated.inc"
#undef BRX_OPTION_DEFAULT
        for (const auto id : {95, 96, 528, 535, 988, 1133, 1134, 1135})
            result[id].kind = BrxPropertyKind::Vector4;
        return result;
    }();
    return definitions;
}

EditorOptionDefinition EditorOptionDefinitionForCid(const std::int16_t cid, const std::int16_t eopid)
{
    EditorOptionDefinition fallback{};
    if (eopid >= 0 && static_cast<std::size_t>(eopid) < EditorOptionDefinitions().size())
        fallback = EditorOptionDefinitions()[static_cast<std::size_t>(eopid)];
    // BuildEopids leaves LIGHT 506/507's old setters commented out and
    // exposes 508-512 with no getter/setter flags. Preserve serialized values
    // when present, but never advertise them as editable overrides.
    if (cid == 81 && eopid >= 506 && eopid <= 512)
        return EditorOptionDefinition{fallback.kind,
                                      0u,
                                      fallback.argumentCount,
                                      fallback.present,
                                      fallback.runtimeDefaultable,
                                      fallback.runtimeDefaultBits};
    return fallback;
}

bool IsRuntimeDefaultValue(const BrxPropertyRecord &property)
{
    if (property.eopid < 0 || static_cast<std::size_t>(property.eopid) >= EditorOptionDefinitions().size())
        return false;
    const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(property.eopid)];
    if (!definition.runtimeDefaultable || property.kind == BrxPropertyKind::Dynamic)
        return false;
    const auto bitsOfFloat = [](const float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    };
    switch (property.kind)
    {
    case BrxPropertyKind::Bool:
        return (property.boolValue ? 1u : 0u) == definition.runtimeDefaultBits;
    case BrxPropertyKind::Float:
        return bitsOfFloat(property.floatValue[0]) == definition.runtimeDefaultBits;
    case BrxPropertyKind::Int32:
        return static_cast<std::uint32_t>(property.intValue) == definition.runtimeDefaultBits;
    case BrxPropertyKind::UInt32:
        return property.uintValue == definition.runtimeDefaultBits;
    case BrxPropertyKind::Int16:
    case BrxPropertyKind::TypedInt16:
        return static_cast<std::uint16_t>(property.intValue) ==
               static_cast<std::uint16_t>(definition.runtimeDefaultBits);
    default:
        return false;
    }
}

namespace
{
struct CidOptionEntry
{
    std::uint16_t cid;
    std::int16_t optionId;
};

// Keep the generated schema as static data. Expanding its roughly 18,000
// entries into individual push_back calls made MSVC give the initialization
// lambda an enormous Debug stack frame, which overflowed during startup.
constexpr CidOptionEntry kCidOptionEntries[]{
#define BRX_CID_OPTION(cid, optionId) CidOptionEntry{ static_cast<std::uint16_t>(cid), static_cast<std::int16_t>(optionId) },
#include "CidOptionSchema.generated.inc"
#undef BRX_CID_OPTION
};
}

const std::array<std::vector<std::int16_t>, 148> &CidOptionIds()
{
    static const auto options = []
    {
        std::array<std::vector<std::int16_t>, 148> result;
        std::array<std::size_t, 148> counts{};
        for (const CidOptionEntry &entry : kCidOptionEntries)
            ++counts[entry.cid];
        for (std::size_t cid = 0; cid < result.size(); ++cid)
            result[cid].reserve(counts[cid]);
        for (const CidOptionEntry &entry : kCidOptionEntries)
            result[entry.cid].push_back(entry.optionId);
        return result;
    }();
    return options;
}

namespace
{
// Authoritative ProjectCane runtime inheritance from the pvtSuper links
// declared by all 148 VTs in Sly1-Release/basic.h.  Effective option sets
// cannot recover this graph: real classes can add no options and unrelated
// classes can expose the same inherited EOPID.
constexpr std::array<std::int16_t, 148> kCidParent{
    -1, 0,  1,   2,   3,   3,   5, 6, 6,  8,  8,  10,  8,   8,   8,   8,  8,   16,  16,  8,   6,   5,   5,   5,   5,
    5,  25, 5,   5,   5,   3,   3, 3, 32, 33, 33, 33,  32,  37,  3,   3,  3,   41,  3,   3,   44,  44,  3,   3,   48,
    48, 48, 3,   3,   3,   54,  3, 3, 3,  3,  59, 60,  61,  59,  3,   3,  3,   3,   3,   3,   3,   3,   3,   3,   3,
    3,  3,  2,   2,   2,   2,   2, 2, 2,  2,  2,  85,  85,  85,  85,  85, 85,  2,   2,   2,   2,   2,   2,   2,   2,
    2,  2,  2,   2,   2,   2,   2, 2, 1,  1,  1,  110, 110, 110, 110, 1,  115, 115, 117, 115, 119, 119, 121, 115, 123,
    2,  1,  126, 126, 126, 126, 1, 1, 1,  1,  1,  1,   1,   1,   1,   1,  1,   1,   1,   1,   1,   0,   0};

bool IsCidInRuntimeChain(const std::int16_t cid, const std::int16_t ancestor)
{
    if (cid < 0 || ancestor < 0 || static_cast<std::size_t>(cid) >= kCidParent.size() ||
        static_cast<std::size_t>(ancestor) >= kCidParent.size())
        return false;
    for (std::int16_t cursor = cid; cursor >= 0; cursor = kCidParent[static_cast<std::size_t>(cursor)])
        if (cursor == ancestor)
            return true;
    return false;
}

bool CidContainsOption(const std::int16_t cid, const std::int16_t eopid)
{
    if (cid < 0 || static_cast<std::size_t>(cid) >= CidOptionIds().size())
        return false;
    const auto &options = CidOptionIds()[static_cast<std::size_t>(cid)];
    return std::find(options.begin(), options.end(), eopid) != options.end();
}
}

std::int16_t PropertyOwnerCid(const std::int16_t objectCid, const std::int16_t eopid)
{
    if (objectCid < 0 || static_cast<std::size_t>(objectCid) >= kCidParent.size())
        return objectCid;
    std::array<std::int16_t, 148> chain{};
    std::size_t count = 0;
    for (std::int16_t cursor = objectCid; cursor >= 0 && count < chain.size();
         cursor = kCidParent[static_cast<std::size_t>(cursor)])
        chain[count++] = cursor;
    while (count != 0)
    {
        const auto candidate = chain[--count];
        if (CidContainsOption(candidate, eopid))
            return candidate;
    }
    return objectCid;
}

std::vector<EditorPropertySection> PropertySectionsForCid(const std::int16_t cid)
{
    std::vector<EditorPropertySection> result;
    if (cid < 0 || static_cast<std::size_t>(cid) >= CidOptionIds().size())
        return {{cid, BrxDocument::ClassName(cid)}};

    static constexpr EditorPropertySection core[] = {
        {0, "BASIC"},       {1, "LO"},        {2, "ALO"},       {3, "SO"},         {4, "MS"},
        {5, "PO"},          {6, "STEP"},      {7, "JT"},        {8, "STEPGUARD"},  {9, "SMARTGUARD"},
        {10, "GOMER"},      {11, "UBG"},      {12, "MBG"},      {13, "BHG"},       {14, "MURRAY"},
        {15, "PUFFC"},      {16, "CRFOD"},    {17, "CRFODB"},   {18, "CRFODK"},    {19, "RUBY"},
        {20, "TANK"},       {21, "ROV"},      {22, "TURRET"},   {23, "VAULT"},     {24, "PUFFER"},
        {25, "SUV"},        {26, "LGN"},      {27, "JETPACK"},  {28, "RYTHM"},     {29, "MGV"},
        {30, "RIPG"},       {31, "WATER"},    {32, "BRK"},      {33, "BREAK"},     {34, "ALBRK"},
        {35, "DARTGUN"},    {36, "SWP"},      {37, "FRAGILE"},  {38, "ZAPBREAK"},  {39, "BRKP"},
        {40, "BUTTON"},     {41, "VOLBTN"},   {42, "JLOVOL"},   {43, "BONE"},      {44, "SPRIZE"},
        {45, "SCPRIZE"},    {46, "CLUE"},     {47, "ALARM"},    {48, "SENSOR"},    {49, "LASEN"},
        {50, "CAMSEN"},     {51, "PRSEN"},    {52, "BARRIER"},  {53, "IKH"},       {54, "TZP"},
        {55, "VOLZP"},      {56, "CHKPNT"},   {57, "CNVO"},     {58, "HBSK"},      {59, "BOMB"},
        {60, "MISSILE"},    {61, "ACCMISS"},  {62, "TARMISS"},  {63, "MINE"},      {64, "FLY"},
        {65, "RAT"},        {66, "ROH"},      {67, "ROC"},      {68, "ROST"},      {69, "ROP"},
        {70, "DART"},       {71, "JLO"},      {72, "PUFFT"},    {73, "MRKV"},      {74, "CLKWORK"},
        {75, "CLKWRKDMG"},  {76, "DECOY"},    {77, "BLIPG"},    {78, "CAMERA"},    {79, "LBONE"},
        {80, "EMITTER"},    {81, "LIGHT"},    {82, "SCH"},      {83, "PROXY"},     {84, "SKY"},
        {85, "DPRIZE"},     {86, "CHARM"},    {87, "LIFETKN"},  {88, "COIN"},      {89, "KEY"},
        {90, "GOLD"},       {91, "BOOST"},    {92, "LOCK"},     {93, "LOCKG"},     {94, "TAIL"},
        {95, "ROB"},        {96, "FLASH"},    {97, "DYSH"},     {98, "WAYPOINT"},  {99, "TN"},
        {100, "JLOC"},      {101, "DIALOG"},  {102, "SPEAKER"}, {103, "WM"},       {104, "CRFODKSPAWN"},
        {105, "HUBSEL"},    {106, "HUBPROG"}, {107, "UNK11"},   {108, "SW"},       {109, "CM"},
        {110, "SHAPE"},     {111, "HSHAPE"},  {112, "PIPE"},    {113, "RAIL"},     {114, "LANDING"},
        {115, "XFM"},       {116, "WARP"},    {117, "TARGET"},  {118, "HND"},      {119, "EXPL"},
        {120, "EXPLG"},     {121, "EXPLO"},   {122, "EXPLS"},   {123, "VOL"},      {124, "RATHOLE"},
        {125, "EXIT"},      {126, "PNT"},     {127, "HPNT"},    {128, "JMT"},      {129, "SPIRE"},
        {130, "SCAN"},      {131, "ASEG"},    {132, "ASEGBL"},  {133, "VISZONE"},  {134, "VISMAP"},
        {135, "FRZG"},      {136, "SM"},      {137, "SGG"},     {138, "PATHZONE"}, {139, "RCHM"},
        {140, "RWM"},       {141, "WR"},      {142, "KEYHOLE"}, {143, "JSG"},      {144, "RYTHMSEQUENCE"},
        {145, "THNDFLASH"}, {146, "ASEGA"},   {147, "SMA"}};
    for (const auto &section : core)
    {
        if (IsCidInRuntimeChain(cid, section.cid))
            result.push_back(section);
    }
    const bool hasConcreteSection = std::any_of(result.begin(), result.end(), [&](const EditorPropertySection &section)
                                                { return section.cid == cid; });
    if (!hasConcreteSection && cid > 3 && (cid == 4 || cid == 27 || cid > 79))
        result.push_back({cid, BrxDocument::ClassName(cid)});
    return result;
}

bool CidDerivesFrom(const std::int16_t cid, const std::int16_t baseCid)
{
    const std::uint32_t key =
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(cid)) << 16 | static_cast<std::uint16_t>(baseCid);
    static std::unordered_map<std::uint32_t, bool> cache;
    if (const auto found = cache.find(key); found != cache.end())
        return found->second;
    const bool derives = IsCidInRuntimeChain(cid, baseCid);
    cache.emplace(key, derives);
    return derives;
}

bool PoClassDefaultPlayable(const std::int16_t cid)
{
    return CidDerivesFrom(cid, 5) && !CidDerivesFrom(cid, 8);
}

namespace
{
bool HasRuntimeBrxUserThunk(const std::int16_t eopid)
{
    // Exhaustive audit of BuildEopids' active (non-commented)
    // pvThunkFnUser assignments whose descriptors carry the 0x400
    // dynamic-call flag. LoadOptionFromBrx invokes these handlers while
    // consuming authored BRX option records. Dynamic EOPIDs not present
    // here are runtime/script operations and must not be emitted as object
    // properties by the editor.
    switch (eopid)
    {
    case 2:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 88:
    case 146:
    case 161:
    case 264:
    case 276:
    case 297:
    case 412:
    case 439:
    case 552:
    case 580:
    case 581:
    case 656:
    case 660:
    case 661:
    case 662:
    case 664:
    case 703:
    case 750:
    case 751:
    case 827:
    case 828:
    case 840:
    case 907:
    case 971:
    case 972:
    case 973:
    case 1003:
    case 1004:
    case 1016:
    case 1020:
    case 1047:
    case 1115:
    case 1118:
    case 1119:
    case 1120:
    case 1121:
    case 1122:
    case 1123:
    case 1124:
    case 1125:
    case 1126:
    case 1127:
    case 1128:
    case 1129:
    case 1131:
    case 1132:
    case 1161:
    case 1162:
    case 1163:
    case 1164:
    case 1165:
    case 1166:
        return true;
    default:
        return false;
    }
}
}

bool IsEditorWritableOption(const std::int16_t eopid)
{
    if (eopid < 0 || static_cast<std::size_t>(eopid) >= EditorOptionDefinitions().size())
        return false;
    const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(eopid)];
    if (definition.present && (definition.flags & 0x400u) != 0 && HasRuntimeBrxUserThunk(eopid))
        return true;
    if (!definition.present || OptionBehavior(eopid) != EditorOptionBehavior::Stored)
        return false;

    // Match LoadOptionFromBrx: scalar BRX overrides require the user-loading
    // lane (ibSetUser or pfnsetUser). Ordinary ibSet/pfnset entries belong to
    // the runtime scripting API. The ensure bits only modify a SetUser target.
    constexpr std::uint32_t setUserMask = 0x80u | 0x100u;
    return (definition.flags & setUserMask) != 0;
}

bool IsEditorWritableOptionForCid(const std::int16_t cid, const std::int16_t eopid)
{
    const auto definition = EditorOptionDefinitionForCid(cid, eopid);
    // CID legality comes from the generated ProjectCane class option table;
    // dynamic persistence comes from the exhaustive BuildEopids user-thunk
    // audit above. Together these match LoadOptionsFromBrx without admitting
    // script-only dynamic calls.
    if (definition.present && (definition.flags & 0x400u) != 0 && HasRuntimeBrxUserThunk(eopid))
        return true;
    if (!definition.present || (definition.flags & 0x1u) != 0 || (definition.flags & 0x400u) != 0)
        return false;
    constexpr std::uint32_t setUserMask = 0x80u | 0x100u;
    return (definition.flags & setUserMask) != 0;
}

bool IsAppendSetUserOption(const std::int16_t eopid)
{
    if (eopid >= 0 && static_cast<std::size_t>(eopid) < EditorOptionDefinitions().size())
    {
        const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(eopid)];
        // LoadOptionFromBrx dispatches every dynamic (0x400) record through its
        // user thunk. These are ordered commands, often with distinct argument
        // payloads (custom sidebag properties, SM goals, skeletons, JSG/rhythm
        // steps), so no occurrence may be replaced merely because the outer
        // EOPID is the same.
        if (definition.present && (definition.flags & 0x400u) != 0)
            return true;
    }

    // Exact BuildEopids ibSetUser/pfnsetUser bindings whose targets are Add*
    // routines rather than stored-field assignments.
    switch (eopid)
    {
    case 204:
    case 222:
    case 223:
    case 224:
    case 251:
    case 252:
    case 253:
    case 254:
    case 255:
    case 256:
    case 267:
    case 268:
    case 275:
    case 285:
    case 287:
    case 289:
    case 290:
    case 291:
    case 299:
    case 300:
    case 301:
    case 302:
    case 344:
    case 345:
    case 610:
    case 619:
    case 620:
    case 694:
    case 695:
    case 708:
    case 775:
    case 820:
    case 1000:
    case 1043:
    case 1059:
        return true;
    default:
        return false;
    }
}

namespace
{
std::int16_t ObjectReferenceBaseCid(const std::int16_t eopid)
{
    switch (eopid)
    {
    case 5:
    case 204:
    case 205:
    case 206:
    case 212:
    case 213:
    case 214:
    case 215:
    case 218:
    case 299:
    case 300:
    case 610:
        return 1; // LO / any world object
    // BARRIER option 344 is consumed by AddBarrierObject and later tested
    // with FMatchesLoName.  It is an OID/name filter, not a load-time
    // pointer binding, so the named object is allowed to be absent from
    // the authored world (or supplied by a runtime/proxy context).
    case 8:
    case 40:
    case 85:
    case 191:
    case 357:
    case 358:
    case 360:
    case 425:
    case 441:
    case 443:
    case 285:
    case 515:
    case 602:
    case 747:
    case 748:
    case 824:
    case 880:
    case 919:
    case 930:
    case 946:
    case 955:
        return 2; // ALO
    case 51:
        return 97; // DYSH silhouette helper rendered into the dynamic shadow texture
    case 701:
        return 96; // FLASH child selected by SMARTGUARD name/OID
    case 700:
        return 123; // VOL used as SMARTGUARD sneak-awareness region
    case 707:
        return 2; // ALO animation-override target
    case 222:
    case 245:
    case 253:
    case 254:
    case 359:
    case 442:
    case 670:
    case 671:
    case 916:
    case 827:
    case 1140:
        return 3; // SO
    case 837:
        return 3; // SO; PostJmtLoad resolves CID 0x104
    case 291:
    case 694:
        return 8; // STEPGUARD and derivatives
    case 771:
        return 67; // ROC collectible template
    case 772:
        return 66; // ROH harvester template
    case 773:
        return 21; // ROV encounter vehicle
    case 774:
        return 69; // ROP prize/reward object
    case 775:
        return 68; // ROST return/spawn tunnel station
    case 289:
        return 34; // ALBRK
    case 290:
        return 48; // SENSOR
    case 708:
        return 52; // BARRIER
    case 816:
        return 70; // DART
    case 1096:
        return 72; // PUFFT
    case 702:
        return 89; // KEY
    case 629:
    case 635:
        return 99; // TN
    case 896:
    case 897:
        return 100; // JLOC
    case 265:
    case 279:
    case 1049:
        return 101; // DIALOG
    case 1094:
    case 1095:
    case 1177:
        return 109; // CM
    case 370:
        return 110; // SHAPE
    // EXPLO does not copy a SHAPE curve here. BindExplo resolves oidShape
    // as an XFM and uses that object's local position and ALO parent as
    // the emitter origin/reference frame.
    case 426:
        return 115; // XFM
    case 844:
    case 848:
        return 113; // RAIL
    // JLOVOL::BindJlovol stores both fields as PNT*/JLOC*. An omitted
    // landing OID searches for a PNT child; it is not a LANDING shape.
    case 894:
    case 895:
        return 126; // PNT
    case 695:
        return 115; // XFM
    // WARP 159 is an alternate lookup identifier for the WARP itself, not
    // a reference to another WARP. Keep it as a free numeric OID.
    // EXIT 163/165 name a WARP in the destination level, so they are not
    // local object references either.
    case 347:
    case 621:
    case 920:
        return 116; // WARP
    case 240:
        return 119; // EXPL
    case 406:
    case 469:
    case 658:
        return 121; // EXPLO
    case 415:
    case 483:
        return 122; // EXPLS
    case 619:
    case 843:
    case 847:
        return 123; // VOL
    case 967:
    case 1089:
        return 126; // PNT camera anchors
    // DIALOG 976 is a persistence-table key, not an object lookup.
    case 1160:
        return 129; // SPIRE
    case 23:
    case 190:
    case 251:
    case 252:
    case 267:
    case 268:
    case 287:
    case 620:
        return 131; // ASEG
    case 121:
    case 122:
    case 185:
    case 186:
    case 520:
        return 132; // ASEGBL
    case 652:
        return 138; // PATHZONE
    case 682:
        return 140; // RWM
    case 523:
        return 125; // EXIT
    case 681:
        return 119; // Guard explosion endpoint; accepts EXPL or EXPLO family below.
    case 820:
        return 65; // RAT
    case 887:
        return 42; // JLOVOL
    case 1000:
        return 92; // LOCK
    case 1048:
        return 41; // VOLBTN
    default:
        return -1;
    }
}
}

bool IsObjectNameFilterOption(const std::int16_t eopid)
{
    // Runtime FMatchesLoName filters store names, not resolved object pointers.
    return eopid == 222 || eopid == 253 || eopid == 254 ||
           eopid == 299 || eopid == 300 || eopid == 344 || eopid == 827;
}

bool SoClassFilterCanMatch(const std::int16_t cid)
{
    // FIsBasicDerivedFrom also matches SO's BASIC, LO and ALO ancestors.
    return CidDerivesFrom(cid, 3) || CidDerivesFrom(3, cid);
}

bool IsObjectReferenceOption(const std::int16_t eopid)
{
    switch (eopid)
    {
    // Union/reference interfaces resolved explicitly by their owning class.
    case 6:
    case 275:
    case 298:
    case 307:
    case 611:
    case 612:
    case 649:
    case 694:
    case 918:
    case 921:
    case 929:
    case 931:
    case 941:
    case 942:
    case 947:
    case 997:
    case 1025:
    case 1037:
    case 1038:
    case 1043:
    case 1059:
    case 1084:
    case 1085:
    case 1106:
    case 1107:
    case 1155:
    case 1159:
        return true;
    default:
        return ObjectReferenceBaseCid(eopid) >= 0;
    }
}

bool ObjectReferenceAccepts(const std::int16_t eopid, const std::int16_t candidateCid)
{
    if (candidateCid < 0)
        return false;
    switch (eopid)
    {
    case 6:
        return candidateCid == 108; // scene world
    case 204:
    case 610:
        // AddZpdZapLo rejects every class outside these six families after
        // resolving the serialized OID during PostZpdLoad.
        return CidDerivesFrom(candidateCid, 126) || // PNT / HPNT
               CidDerivesFrom(candidateCid, 110) || // SHAPE / HSHAPE
               CidDerivesFrom(candidateCid, 118) || // HND
               CidDerivesFrom(candidateCid, 112) || // PIPE
               CidDerivesFrom(candidateCid, 113) || // RAIL
               CidDerivesFrom(candidateCid, 129);   // SPIRE
    case 275:
        // UpdateVolbtn explicitly reads positions from CM, PNT and XFM before
        // accepting an ordinary ALO. Other LO classes have no supported
        // position representation and are skipped by the runtime.
        return CidDerivesFrom(candidateCid, 109) || CidDerivesFrom(candidateCid, 126) ||
               CidDerivesFrom(candidateCid, 115) || CidDerivesFrom(candidateCid, 2);
    // Despite the historical OTYP_Bone name, these fields are ALARM*
    // back-references installed by PostAlarmLoadCallbackHookup through
    // SetAlbrkAlarm/SetSensorAlarm.  They never point at CID_BONE.
    case 298:
    case 307:
        return CidDerivesFrom(candidateCid, 47); // ALARM
    case 611:
    case 612:
        return CidDerivesFrom(candidateCid, 1);
    case 649:
        // PostPipeLoad performs a contextual mode-4 LO lookup. PIPE keeps the
        // result as an opaque runtime pointer and does not dereference it.
        return CidDerivesFrom(candidateCid, 1);
    case 650:
        // PIPE::pshape is a runtime SHAPE pointer (despite OTYP_Rope).
        return CidDerivesFrom(candidateCid, 110);
    case 1037:
    case 1038:
        // PostRailLoad resolves this through contextual mode 4, then rail
        // movement treats the result as SHAPE and dereferences pcrv.
        return CidDerivesFrom(candidateCid, 110);
    case 918: // RWM fire source: PNT, XFM or ALO
        return CidDerivesFrom(candidateCid, 126) || CidDerivesFrom(candidateCid, 115) ||
               CidDerivesFrom(candidateCid, 2);
    case 941: // RWM aim source: XFM or ALO
        return CidDerivesFrom(candidateCid, 115) || CidDerivesFrom(candidateCid, 2);
    case 929: // RWM target source: TARGET or ALO
        return CidDerivesFrom(candidateCid, 117) || CidDerivesFrom(candidateCid, 2);
    case 1025:
    case 1106:
    case 1107:
    case 1155:
    case 1159:
        // These properties are stored as XFM pointers by their owning classes.
        // Accept XFM-derived records only; accepting every LO makes it possible
        // to serialize an OID that the runtime later treats as an XFM.
        return CidDerivesFrom(candidateCid, 115);
    case 921:
    case 931:
    case 942:
        // JACK nodes are internal transform-like LO objects and do not have a
        // root CID in the BRX dispatch table.
        return CidDerivesFrom(candidateCid, 1);
    case 947:
        return CidDerivesFrom(candidateCid, 2);
    case 602:
        // PostCameraLoad resolves either an ALO target or a standalone PNT.
        return CidDerivesFrom(candidateCid, 2) || CidDerivesFrom(candidateCid, 126);
    case 681:
        // BindStepguard performs an untyped contextual lookup and stores the
        // result in the guard explosion slot.  Retail worlds legitimately
        // supply either an EXPL transform (CID 119) or an EXPLO-derived
        // emitter such as EXPLS (CID 122); both expose the explosion dispatch
        // consumed by the guard/step-zap paths.
        return CidDerivesFrom(candidateCid, 119) || CidDerivesFrom(candidateCid, 121);
    case 997:
        return candidateCid == 136; // SM
    case 1043:
        // AddTurretTargetOid stores a target which PostTurretLoad resolves and
        // casts to SO*. UpdateTurretAim/FFilterTurret then dereference SO
        // collision fields, so accepting a generic ALO (or TARGET helper)
        // would create an invalid runtime pointer contract.
        return CidDerivesFrom(candidateCid, 3);
    case 1059:
        return CidDerivesFrom(candidateCid, 2);
    case 1084:
    case 1085:
        return CidDerivesFrom(candidateCid, 1);
    case 694:
        return CidDerivesFrom(candidateCid, 8);
    default:
        if (const std::int16_t baseCid = ObjectReferenceBaseCid(eopid); baseCid >= 0)
            return candidateCid == baseCid || CidDerivesFrom(candidateCid, baseCid);
        return false;
    }
}

const char *ObjectReferenceTypeName(const std::int16_t eopid)
{
    switch (eopid)
    {
    case 6:
        return "SW";
    case 204:
        return "PNT, SHAPE, HND, PIPE, RAIL, or SPIRE";
    case 275:
        return "CM, PNT, XFM, or ALO";
    case 298:
    case 307:
        return "ALARM";
    case 611:
    case 612:
        return "LO";
    case 947:
        return "ALO";
    case 997:
        return "SM";
    case 918:
        return "PNT, XFM, or ALO";
    case 941:
        return "XFM or ALO";
    case 929:
        return "TARGET or ALO";
    case 1043:
        return "SO";
    case 222:
        return "SO";
    case 1059:
        return "ALO spawn-slot root";
    case 602:
        return "ALO or PNT";
    case 285:
        return "ALO freeze-group member";
    case 694:
        return "STEPGUARD";
    case 695:
        return "XFM search marker";
    case 747:
        return "ALO reach host";
    case 748:
        return "ALO touch joint beneath the host";
    case 649:
        return "contextual LO";
    case 650:
        return "SHAPE";
    case 1037:
    case 1038:
        return "SHAPE control-direction curve";
    case 1025:
    case 1106:
    case 1107:
    case 1155:
    case 1159:
        return "XFM";
    case 921:
    case 931:
    case 942:
        return "JACK/transform-compatible LO";
    default:
        if (const std::int16_t cid = ObjectReferenceBaseCid(eopid); cid >= 0)
            return BrxDocument::ClassName(cid);
        return "compatible object";
    }
}

namespace
{
struct OptionConflict
{
    std::int16_t first;
    std::int16_t second;
    const char *reason;
};

// These pairs write the same ProjectCane runtime state. Keeping both in a
// BRX makes the result depend on property application order, so the editor
// treats them as mutually exclusive instead of silently choosing a winner.
constexpr OptionConflict kBaseOptionConflicts[]{
    {32, 35, "Both values set the object's current local orientation."},
    {52, 53, "Both values set the object's freeze mode."},
    {54, 55, "Restore position and its explicit response both set the position acknowledgement mode."},
    {56, 57, "Use either the position-spring scale or the complete spring curve."},
    {58, 59, "Use either the position-damping scale or the complete damping curve."},
    {60, 61, "Restore rotation and its explicit response both set the rotation acknowledgement mode."},
    {63, 64, "Use either the rotation-spring scale or the complete spring curve."},
    {65, 66, "Use either the rotation-damping scale or the complete damping curve."},
    {67, 68, "Use either the position-smoothing scale or the complete smoothing settings."},
    {69, 70, "Use either the rotation-smoothing scale or the complete smoothing settings."},
    {864, 865,
     "Focus ratio expands into the focus vector; serializing both makes load order decide the final vector."}};

const BrxPropertyRecord *FindProperty(const std::vector<BrxPropertyRecord> &properties, const std::int16_t option)
{
    const auto found = std::find_if(properties.begin(), properties.end(),
                                    [&](const BrxPropertyRecord &property) { return property.eopid == option; });
    return found == properties.end() ? nullptr : &*found;
}

bool FiniteFloats(const BrxPropertyRecord &property, const int count)
{
    for (int component = 0; component < count; ++component)
        if (!std::isfinite(property.floatValue[component]))
            return false;
    return true;
}

bool BooleanOptionEnabled(const std::vector<BrxPropertyRecord> &properties, const std::int16_t option)
{
    const auto *property = FindProperty(properties, option);
    return property != nullptr && property->boolValue;
}
}

bool CanEnableBaseOption(const std::int16_t cid, const std::vector<BrxPropertyRecord> &properties,
                         const std::int16_t option, std::string *reason)
{
    if (!CidDerivesFrom(cid, 2))
        return true;
    if (cid == 99 && (option == 864 || option == 865))
    {
        const std::int16_t other = option == 864 ? 865 : 864;
        if (FindProperty(properties, other) != nullptr)
        {
            if (reason != nullptr)
                *reason = "Focus ratio and Focus vector write the same TN focus data. Reset " +
                          SafePropertyLabel(other) + " to the game default first.";
            return false;
        }
    }
    // Shorthand and detailed variants are both legal in retail BRX data. If
    // both are present, load order decides the final runtime value; report it
    // as a warning rather than preventing faithful editing.
    // ProjectCane's animation, throb, and sound detail setters call their
    // respective Ensure helpers.  They are therefore legal without the
    // headline option already being serialized.  Validation still reports a
    // warning when such a record is inert or relies on an implicit default.
    // Shadow detail setters ensure a SHADOW record. They remain legal beside
    // Cast shadow=false; the later serialized option determines whether the
    // record is removed or recreated.
    if (CidDerivesFrom(cid, 3))
    {
        const auto rejectWhenPresent = [&](const std::int16_t other, const char *explanation)
        {
            if (FindProperty(properties, other) == nullptr)
                return true;
            if (reason != nullptr)
                *reason =
                    std::string(explanation) + " Reset " + SafePropertyLabel(other) + " to the game default first.";
            return false;
        };
        if ((option == 131 || option == 132) &&
            !rejectWhenPresent(130, "No interaction locks both force and torque constraints."))
            return false;
        if (option == 130 && (!rejectWhenPresent(131, "No interaction replaces the force constraint.") ||
                              !rejectWhenPresent(132, "No interaction replaces the torque constraint.")))
            return false;
        // Custom gravity and the narrower contact switches are intentionally
        // allowed while their broader disable switch is active. The engine
        // preserves those authored values; they are merely dormant until the
        // broad switch is removed, so validation reports a warning instead.
        // EOPIDs 141 and 148 write independent SO flags. Keeping both is
        // legal, though the resulting runtime policy is usually surprising.
    }
    return true;
}

std::vector<EditorOptionIssue> ValidateBaseOptions(const std::int16_t cid,
                                                   const std::vector<BrxPropertyRecord> &properties)
{
    std::vector<EditorOptionIssue> issues;
    const auto add = [&](const EditorOptionIssueSeverity severity, const std::int16_t option,
                         const std::int16_t related, const std::string &message)
    { issues.push_back({severity, option, related, message}); };

    for (std::size_t index = 0; index < properties.size(); ++index)
    {
        const auto &property = properties[index];
        if (!EditorOptionDefinitionForCid(cid, property.eopid).present)
            add(EditorOptionIssueSeverity::Error, property.eopid, -1,
                "This property is not legal for " + SafeClassLabel(cid) + ".");
        // LoadOptionsFromBrx is an ordered command stream.  It places no
        // uniqueness constraint on EOPIDs: ordinary setters legally use
        // last-write-wins semantics, while custom thunks frequently append or
        // alter the interpretation of following records.  The structured BRX
        // writer therefore preserves every occurrence and its order.
    }

    if (const auto *limits = FindProperty(properties, 3);
        limits != nullptr && FiniteFloats(*limits, 2) && limits->floatValue[0] > limits->floatValue[1])
        add(EditorOptionIssueSeverity::Error, 3, -1, "Suck/hide minimum is greater than its maximum.");

    if (!CidDerivesFrom(cid, 2))
        return issues;

    for (const auto &conflict : kBaseOptionConflicts)
        if (FindProperty(properties, conflict.first) != nullptr && FindProperty(properties, conflict.second) != nullptr)
            add(EditorOptionIssueSeverity::Warning, conflict.first, conflict.second,
                std::string(conflict.reason) + " ProjectCane applies the later serialized value.");

    const auto requireConfigured = [&](const std::int16_t option, const std::int16_t owner, const char *message)
    {
        if (FindProperty(properties, option) == nullptr)
            return;
        const auto *ownerProperty = FindProperty(properties, owner);
        if (ownerProperty == nullptr || ownerProperty->intValue < 0)
            add(EditorOptionIssueSeverity::Warning, option, owner, message);
    };
    requireConfigured(24, 23, "Animation local time has no configured animation-segment OID on this object.");
    requireConfigured(25, 23, "Animation playback rate has no configured animation-segment OID on this object.");
    requireConfigured(26, 23, "Animation render priority has no configured animation-segment OID on this object.");
    for (std::int16_t option = 98; option <= 100; ++option)
        requireConfigured(option, 97, "Custom throb settings are present without a configured throb mode.");
    for (std::int16_t option = 102; option <= 109; ++option)
        if (option != 107 && option != 108)
            requireConfigured(option, 101, "Sound settings are present without a configured sound-effect ID.");

    if (const auto *cast = FindProperty(properties, 43); cast != nullptr && !cast->boolValue)
        for (std::int16_t option = 44; option <= 51; ++option)
            if (FindProperty(properties, option) != nullptr)
            {
                add(EditorOptionIssueSeverity::Warning, option, 43,
                    "Shadow details and Cast shadow=false are both serialized; ProjectCane's application order determines whether the shadow is removed or recreated.");
                break;
            }

    const auto nonNegative = [&](const std::int16_t option, const char *label)
    {
        if (const auto *value = FindProperty(properties, option);
            value != nullptr && (!FiniteFloats(*value, 1) || value->floatValue[0] < 0.0f))
            add(EditorOptionIssueSeverity::Error, option, -1, std::string(label) + " must be finite and non-negative.");
    };
    // Do not apply this rule to EOPID 41, 45, or 46. ProjectCane treats a
    // non-positive fast-shadow radius as disabled, and projected-shadow radii
    // deliberately use -1 as the unset/infer-from-the-other-radius sentinel.
    for (const auto option : {42, 47, 48, 77, 92, 102, 103, 109})
        nonNegative(static_cast<std::int16_t>(option), PropertyName(static_cast<std::int16_t>(option)));

    for (const auto option : {41, 45, 46})
        if (const auto *value = FindProperty(properties, static_cast<std::int16_t>(option));
            value != nullptr && !FiniteFloats(*value, 1))
            add(EditorOptionIssueSeverity::Error, static_cast<std::int16_t>(option), -1,
                std::string(PropertyName(static_cast<std::int16_t>(option))) + " must be finite.");

    if (const auto *angle = FindProperty(properties, 49);
        angle != nullptr &&
        (!FiniteFloats(*angle, 1) || angle->floatValue[0] <= 0.0f || angle->floatValue[0] >= 180.0f))
        add(EditorOptionIssueSeverity::Error, 49, -1,
            "Shadow cone angle must be greater than 0 and less than 180 degrees.");
    if (const auto *duration = FindProperty(properties, 100);
        duration != nullptr && (!FiniteFloats(*duration, 1) || duration->floatValue[0] <= 0.0f))
        add(EditorOptionIssueSeverity::Error, 100, -1, "Throb transition time must be greater than zero.");
    const auto integerRange = [&](const std::int16_t option, const int minimum, const int maximum, const char *label)
    {
        if (const auto *value = FindProperty(properties, option);
            value != nullptr && (value->intValue < minimum || value->intValue > maximum))
            add(EditorOptionIssueSeverity::Error, option, -1,
                std::string(label) + " is outside the game's valid range (" + std::to_string(minimum) + " to " +
                    std::to_string(maximum) + ").");
    };
    integerRange(53, 0, 2, "Freeze behavior");
    for (const auto option : {55, 61, 73, 74, 76})
        integerRange(static_cast<std::int16_t>(option), -1, 9, PropertyName(static_cast<std::int16_t>(option)));
    integerRange(97, -1, 5, "Color-throb mode");
    if (const auto *repeat = FindProperty(properties, 106);
        repeat != nullptr && FiniteFloats(*repeat, 2) && repeat->floatValue[0] > repeat->floatValue[1])
        add(EditorOptionIssueSeverity::Error, 106, -1, "Sound repeat-delay minimum is greater than its maximum.");
    if (const auto *start = FindProperty(properties, 102); start != nullptr)
        if (const auto *full = FindProperty(properties, 103); full != nullptr && FiniteFloats(*start, 1) &&
                                                              FiniteFloats(*full, 1) &&
                                                              full->floatValue[0] > start->floatValue[0])
            add(EditorOptionIssueSeverity::Warning, 103, 102,
                "Full-volume distance is farther than sound start distance.");

    for (const auto option : {75, 95, 96})
        if (const auto *color = FindProperty(properties, static_cast<std::int16_t>(option)); color != nullptr)
            for (int component = 0; component < 4; ++component)
                if (!std::isfinite(color->floatValue[component]) || color->floatValue[component] < 0.0f ||
                    color->floatValue[component] > 1.0f)
                {
                    add(EditorOptionIssueSeverity::Warning, static_cast<std::int16_t>(option), -1,
                        SafePropertyLabel(static_cast<std::int16_t>(option)) +
                            " contains a component outside the normalized 0-1 range.");
                    break;
                }

    if (CidDerivesFrom(cid, 3))
    {
        const auto conflictWhenEnabled = [&](const std::int16_t first, const std::int16_t second, const char *message)
        {
            if (FindProperty(properties, first) != nullptr && FindProperty(properties, second) != nullptr)
                add(EditorOptionIssueSeverity::Info, first, second, message);
        };

        conflictWhenEnabled(
            130, 131,
            "No interaction and force constraint are applied sequentially; the editor preserves their authored order.");
        conflictWhenEnabled(
            130, 132,
            "No interaction and torque constraint are applied sequentially; the editor preserves their authored order.");
        if (BooleanOptionEnabled(properties, 138) && FindProperty(properties, 129) != nullptr)
            add(EditorOptionIssueSeverity::Warning, 138, 129,
                "No gravity currently suppresses the custom gravity vector. The vector is preserved and becomes effective again when No gravity is disabled.");
        if (BooleanOptionEnabled(properties, 139) && BooleanOptionEnabled(properties, 140))
            add(EditorOptionIssueSeverity::Info, 139, 140,
                "Hierarchy-wide contact suppression already includes self contacts; the narrower setting is currently redundant.");
        if (BooleanOptionEnabled(properties, 139) && BooleanOptionEnabled(properties, 141))
            add(EditorOptionIssueSeverity::Info, 139, 141,
                "Hierarchy-wide contact suppression already includes center contacts; the narrower setting is currently redundant.");
        if (BooleanOptionEnabled(properties, 139) && BooleanOptionEnabled(properties, 148))
            add(EditorOptionIssueSeverity::Warning, 139, 148,
                "Hierarchy-wide contact suppression currently overrides enabled center contacts. The center setting is preserved for later use.");
        if (BooleanOptionEnabled(properties, 141) && BooleanOptionEnabled(properties, 148))
            add(EditorOptionIssueSeverity::Warning, 141, 148,
                "Center-contact generation is disabled while center-XP behavior is enabled; ProjectCane stores these as independent flags.");

        if (const auto *noInteraction = FindProperty(properties, 130);
            noInteraction != nullptr && !noInteraction->boolValue)
            add(EditorOptionIssueSeverity::Warning, 130, -1,
                "No interaction is serialized as false, but ProjectCane's setter ignores the value and locks both constraints whenever invoked. Remove this override instead.");

        const auto positive = [&](const std::int16_t option, const char *label)
        {
            if (const auto *value = FindProperty(properties, option);
                value != nullptr && (!FiniteFloats(*value, 1) || value->floatValue[0] <= 0.0f))
                add(EditorOptionIssueSeverity::Error, option, -1,
                    std::string(label) + " must be finite and greater than zero.");
        };
        positive(127, "Mass");
        positive(133, "Sphere collision radius");
        for (const auto option : {134, 135})
            nonNegative(static_cast<std::int16_t>(option), PropertyName(static_cast<std::int16_t>(option)));
        if (const auto *gravity = FindProperty(properties, 129); gravity != nullptr && !FiniteFloats(*gravity, 3))
            add(EditorOptionIssueSeverity::Error, 129, -1, "Gravity vector contains a non-finite component.");
        // Retail levels use the terminal MTLK value 12 for some hidden SO
        // triggers, and ProjectCane's SetSoMtlk accepts it verbatim.
        integerRange(128, -1, 12, "Material kind");
        integerRange(131, -1, 7, "Force constraint");
        integerRange(132, -1, 7, "Torque constraint");
        integerRange(143, -1, 4, "Collision zone kind");
        integerRange(145, -1, 2, "Camera collision style");
    }
    return issues;
}

EditorOptionGroup OptionGroup(const std::int16_t eopid)
{
    if (eopid <= 20)
        return EditorOptionGroup::Identity;
    if (eopid >= 24 && eopid <= 27)
        return EditorOptionGroup::Rendering;
    if (eopid <= 40)
        return EditorOptionGroup::Transform;
    if (eopid <= 51)
        return EditorOptionGroup::Rendering;
    if (eopid <= 74)
        return EditorOptionGroup::Motion;
    if (eopid == 75 || eopid == 123 || eopid == 125)
        return EditorOptionGroup::Rendering;
    if (eopid <= 94)
        return EditorOptionGroup::LookAt;
    if (eopid <= 100)
        return EditorOptionGroup::Effects;
    if (eopid <= 109)
        return EditorOptionGroup::Audio;
    if (eopid <= 116)
        return EditorOptionGroup::Interaction;
    if (eopid <= 126)
        return EditorOptionGroup::Runtime;
    if (eopid >= 491 && eopid <= 512)
        return EditorOptionGroup::Rendering;
    if (eopid == 608)
        return EditorOptionGroup::Transform;
    if (eopid >= 979 && eopid <= 987)
        return EditorOptionGroup::Interaction;
    if (eopid == 131 || eopid == 132)
        return EditorOptionGroup::Constraints;
    if (eopid == 128 || eopid == 130 || eopid == 133 || eopid == 136 || (eopid >= 139 && eopid <= 148))
        return EditorOptionGroup::Collision;
    if (eopid >= 127 && eopid <= 138)
        return EditorOptionGroup::Physics;
    if (eopid >= 247 && eopid <= 280)
        return EditorOptionGroup::Interaction;
    if (eopid >= 286 && eopid <= 312)
        return EditorOptionGroup::Interaction;
    if (eopid >= 313 && eopid <= 336)
        return EditorOptionGroup::Interaction;
    if (eopid >= 344 && eopid <= 347)
        return EditorOptionGroup::Interaction;
    if (eopid >= 609 && eopid <= 621)
        return EditorOptionGroup::Interaction;
    if (eopid == 625 || (eopid >= 640 && eopid <= 642))
        return EditorOptionGroup::Motion;
    if (eopid >= 712 && eopid <= 731)
        return EditorOptionGroup::Effects;
    if (eopid >= 752 && eopid <= 762)
        return EditorOptionGroup::CharacterAI;
    if (eopid >= 805 && eopid <= 809)
        return EditorOptionGroup::CharacterAI;
    if (eopid >= 810 && eopid <= 815)
        return EditorOptionGroup::CharacterCombat;
    if (eopid >= 823 && eopid <= 835)
        return EditorOptionGroup::Motion;
    if (eopid >= 887 && eopid <= 893)
        return EditorOptionGroup::Interaction;
    if (eopid >= 948 && eopid <= 965)
        return EditorOptionGroup::CharacterAI;
    if (eopid >= 1054 && eopid <= 1056)
        return EditorOptionGroup::Rendering;
    if (eopid >= 1140 && eopid <= 1149)
        return EditorOptionGroup::CharacterCombat;
    if (eopid >= 1176 && eopid <= 1178)
        return EditorOptionGroup::CharacterCombat;
    if (eopid == 622 || eopid == 624 || eopid == 895 || eopid == 897)
        return EditorOptionGroup::Runtime;
    if (eopid == 623)
        return EditorOptionGroup::Effects;
    if (eopid == 894 || eopid == 896)
        return EditorOptionGroup::Interaction;
    if (eopid == 652 || (eopid >= 670 && eopid <= 677) || eopid == 680 || eopid == 686 || eopid == 688)
        return EditorOptionGroup::CharacterAI;
    if ((eopid >= 656 && eopid <= 664) || eopid == 683 || eopid == 685 || eopid == 687 || eopid == 689)
        return EditorOptionGroup::CharacterAnimation;
    if ((eopid >= 653 && eopid <= 655) || eopid == 665 || eopid == 666 || (eopid >= 678 && eopid <= 682) ||
        (eopid >= 690 && eopid <= 692))
        return EditorOptionGroup::CharacterCombat;
    if (eopid == 667 || eopid == 668 || eopid == 669 || eopid == 684 || eopid == 693)
        return EditorOptionGroup::Motion;
    if (eopid >= 696 && eopid <= 708)
        return EditorOptionGroup::CharacterAI;
    if (eopid >= 841 && eopid <= 850)
        return EditorOptionGroup::CharacterAI;
    if (eopid == 1023 || eopid == 1025)
        return EditorOptionGroup::CharacterAI;
    if (eopid == 1024 || eopid == 1026 || eopid == 1027)
        return EditorOptionGroup::Runtime;
    if (eopid == 1028 || eopid == 1029)
        return EditorOptionGroup::CharacterAI;
    if (eopid == 804)
        return EditorOptionGroup::Motion;
    if (eopid == 1039)
        return EditorOptionGroup::Runtime;
    if (eopid >= 1040 && eopid <= 1045)
        return EditorOptionGroup::LookAt;
    if (eopid == 1079)
        return EditorOptionGroup::CharacterCombat;
    if (eopid == 1105)
        return EditorOptionGroup::Runtime;
    if (eopid == 1106 || eopid == 1107)
        return EditorOptionGroup::CharacterAI;
    if (eopid == 1167 || eopid == 1169)
        return EditorOptionGroup::CharacterCombat;
    if (eopid == 1168)
        return EditorOptionGroup::CharacterAnimation;
    if (eopid == 1150 || eopid == 1151 || eopid == 1155)
        return EditorOptionGroup::CharacterAI;
    if (eopid == 1152 || eopid == 1153 || eopid == 1154 || eopid == 1156 || eopid == 1157 || eopid == 1158)
        return EditorOptionGroup::Runtime;
    return EditorOptionGroup::General;
}

const char *OptionGroupName(const EditorOptionGroup group)
{
    switch (group)
    {
    case EditorOptionGroup::Identity:
        return "Identity & hierarchy";
    case EditorOptionGroup::Transform:
        return "Transform & velocity";
    case EditorOptionGroup::Rendering:
        return "Rendering & shadows";
    case EditorOptionGroup::Motion:
        return "Motion controllers";
    case EditorOptionGroup::LookAt:
        return "Look-at & targeting";
    case EditorOptionGroup::Effects:
        return "Material effects";
    case EditorOptionGroup::Audio:
        return "Audio";
    case EditorOptionGroup::Interaction:
        return "Interactions & poses";
    case EditorOptionGroup::Physics:
        return "Physics";
    case EditorOptionGroup::Constraints:
        return "Constraints";
    case EditorOptionGroup::Collision:
        return "Collision & contacts";
    case EditorOptionGroup::Runtime:
        return "Runtime actions & state";
    case EditorOptionGroup::CharacterAI:
        return "Guard AI & targeting";
    case EditorOptionGroup::CharacterAnimation:
        return "Guard animation & state";
    case EditorOptionGroup::CharacterCombat:
        return "Guard combat & damage";
    default:
        return "Class-specific";
    }
}

EditorOptionBehavior OptionBehavior(const std::int16_t eopid)
{
    if (eopid < 0 || static_cast<std::size_t>(eopid) >= EditorOptionDefinitions().size())
        return EditorOptionBehavior::Derived;
    const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(eopid)];
    // SO contact/BSP entries are live engine queries even though their legacy
    // flag patterns overlap action entries. They must never be serialized as
    // editable constants by the map editor.
    if (eopid == 142 || eopid == 144 || eopid == 147 || eopid == 283 || eopid == 844 || eopid == 848)
        return EditorOptionBehavior::Query;
    if ((definition.flags & 0x400u) != 0)
        return EditorOptionBehavior::Expression;
    if ((definition.flags & 0x1u) != 0)
        return EditorOptionBehavior::Action;

    constexpr std::int16_t derivedIds[] = {31, 34, 38, 39, 40};
    if (std::find(std::begin(derivedIds), std::end(derivedIds), eopid) != std::end(derivedIds))
        return EditorOptionBehavior::Derived;
    // An ordinary runtime setter is not a BRX-authorable override. Only the
    // SetUser field/function lanes are writable by the property editor.
    constexpr std::uint32_t setUserMask = 0x80u | 0x100u;
    if ((definition.flags & setUserMask) == 0)
        return EditorOptionBehavior::Query;
    return EditorOptionBehavior::Stored;
}

const char *OptionBehaviorName(const EditorOptionBehavior behavior)
{
    switch (behavior)
    {
    case EditorOptionBehavior::Stored:
        return "stored override";
    case EditorOptionBehavior::Derived:
        return "derived value";
    case EditorOptionBehavior::Query:
        return "runtime query";
    case EditorOptionBehavior::Action:
        return "runtime action";
    case EditorOptionBehavior::Expression:
        return "expression-backed";
    }
    return "unknown";
}

const char *PropertyKindName(const BrxPropertyKind kind)
{
    switch (kind)
    {
    case BrxPropertyKind::Bool:
        return "bool";
    case BrxPropertyKind::Float:
        return "float";
    case BrxPropertyKind::Vector3:
        return "vector3";
    case BrxPropertyKind::Vector4:
        return "vector4";
    case BrxPropertyKind::Int32:
        return "int32";
    case BrxPropertyKind::Float2:
        return "float2";
    case BrxPropertyKind::UInt32:
        return "uint32";
    case BrxPropertyKind::Int16:
        return "int16/enum";
    case BrxPropertyKind::TypedInt16:
        return "typed int16";
    case BrxPropertyKind::Dynamic:
        return "dynamic expression";
    }
    return "unknown";
}

void ResetPropertyValue(BrxPropertyRecord &property)
{
    property.boolValue = false;
    property.intValue = 0;
    property.uintValue = 0;
    property.typeValue = 0;
    std::fill(std::begin(property.floatValue), std::end(property.floatValue), 0.0f);
    // ALO ensure-object defaults. Enabling just one ASEGD, SHADOW, motion, or
    // SFX override constructs the whole runtime side record, so initialize the
    // edited value from that record's real defaults rather than scalar zero.
    if (property.eopid == 23 || property.eopid == 44 || property.eopid == 51 || property.eopid == 101)
        property.intValue = -1;
    if (property.eopid == 43)
        property.boolValue = true;
    if (property.eopid == 25)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 45 || property.eopid == 46)
        property.floatValue[0] = -1.0f;
    if (property.eopid == 47)
        property.floatValue[0] = 100.0f;
    if (property.eopid == 48)
        property.floatValue[0] = 400.0f;
    if (property.eopid == 50)
        property.floatValue[1] = 1.0f;
    if (property.eopid == 55 || property.eopid == 61 || property.eopid == 73 || property.eopid == 74 ||
        property.eopid == 76)
        property.intValue = -1;
    if (property.eopid == 82)
        property.intValue = 1;
    if (property.eopid == 83 || property.eopid == 84)
        property.intValue = -1;
    if (property.eopid == 56 || property.eopid == 58 || property.eopid == 63 || property.eopid == 65 ||
        property.eopid == 67 || property.eopid == 69)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 57)
    {
        property.floatValue[1] = 15.0f;
        property.floatValue[2] = 0.015f;
    }
    if (property.eopid == 59)
    {
        property.floatValue[1] = 2.5f;
        property.floatValue[2] = 0.0025f;
    }
    if (property.eopid == 64)
    {
        property.floatValue[1] = 15.0f;
        property.floatValue[2] = 1.5f;
    }
    if (property.eopid == 66)
    {
        property.floatValue[1] = 2.5f;
        property.floatValue[2] = 0.25f;
    }
    if (property.eopid == 68)
    {
        property.floatValue[0] = 1500.0f;
        property.floatValue[2] = 0.5f;
        property.floatValue[3] = 4500.0f;
    }
    if (property.eopid == 70)
    {
        property.floatValue[0] = 15.0f;
        property.floatValue[2] = 0.5f;
        property.floatValue[3] = 45.0f;
    }
    if (property.eopid == 102)
        property.floatValue[0] = 3000.0f;
    if (property.eopid == 103)
        property.floatValue[0] = 300.0f;
    if (property.eopid == 104)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 106)
        property.floatValue[0] = -1.0f;
    // InitSo copies the normal Scene World gravity; EOP 129 replaces the
    // complete vector. The retail non-cheat world default is -1500 on Z.
    if (property.eopid == 129)
        property.floatValue[2] = -1500.0f;
    if (property.eopid == 130)
        property.boolValue = true;
    // These are explicit fDef values in the SO EOP descriptors.
    if (property.eopid == 134)
        property.floatValue[0] = 0.5f;
    if (property.eopid == 135)
        property.floatValue[0] = 1.0f;
    // InitPo registers a PO as playable unless the BRX explicitly disables it.
    // Enabling the friendly override must begin at that class default instead
    // of silently turning the character off.
    if (property.eopid == 281)
        property.boolValue = true;
    // Defaults established by InitStep/BuildEopids. These cannot safely be
    // represented by the generic numeric zero used for most new overrides.
    if (property.eopid == 709)
        property.floatValue[0] = 35.0f;
    if (property.eopid == 710 || property.eopid == 711)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 735)
        property.boolValue = true;
    if (property.eopid == 736)
        property.intValue = -1;
    if (property.eopid == 652 || property.eopid == 670 || property.eopid == 681 || property.eopid == 682)
        property.intValue = -1;
    // InitStepGuard is zero-initialized and does not assign rpk, so the class
    // default is RPK_Closest (0), not RPK_Nil (-1).
    if (property.eopid == 676)
        property.intValue = 0;
    if (property.eopid == 653)
    {
        property.floatValue[0] = 0.0f;
        property.floatValue[1] = 300.0f;
    }
    if (property.eopid == 654)
    {
        property.floatValue[0] = 0.2f;
        property.floatValue[1] = 0.5f;
    }
    // EOPID 655 is authored in degrees. ProjectCane's user setter converts
    // this serialized value to the runtime radAttackMax field.
    if (property.eopid == 655)
        property.floatValue[0] = 10.0f;
    if (property.eopid == 668)
    {
        property.floatValue[0] = 1.0f;
        property.floatValue[1] = 2.5f;
    }
    if (property.eopid == 675)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 678)
        property.intValue = -1;
    if (property.eopid == 683)
        property.floatValue[0] = -1.0f;
    if (property.eopid == 684)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 685 || property.eopid == 690)
        property.boolValue = true;
    if (property.eopid == 689)
    {
        property.floatValue[0] = 0.0f;
        property.floatValue[1] = 0.8f;
    }
    if (property.eopid == 691)
        property.floatValue[0] = 1.25f;
    if (property.eopid == 696 || property.eopid == 697 || property.eopid == 699)
        property.floatValue[0] = 300.0f;
    if (property.eopid == 698)
        property.floatValue[0] = 2000.0f;
    if (property.eopid == 700 || property.eopid == 701 || property.eopid == 707 || property.eopid == 708)
        property.intValue = -1;
    if (property.eopid == 841)
        property.floatValue[0] = 2000.0f;
    if (property.eopid == 842)
        property.floatValue[0] = 300.0f;
    if (property.eopid == 843 || property.eopid == 847)
        property.intValue = -1;
    if (property.eopid == 845)
    {
        property.floatValue[0] = 1000.0f;
        property.floatValue[1] = 1500.0f;
    }
    if (property.eopid == 846)
    {
        property.floatValue[0] = 0.7853982f;
        property.floatValue[1] = 3.1415927f;
    }
    // InitSpeaker defaults. A newly enabled override must begin at the runtime
    // default rather than zeroing the portrait camera, placement, or color.
    if (property.eopid == 988)
        std::fill(std::begin(property.floatValue), std::end(property.floatValue), 1.0f);
    if (property.eopid == 989)
        property.floatValue[0] = 0.5f;
    if (property.eopid == 990)
        property.floatValue[0] = 400.0f;
    if (property.eopid == 991 || property.eopid == 992 || property.eopid == 996)
        property.floatValue[0] = 0.5f;
    if (property.eopid == 995)
        property.floatValue[0] = 0.3f;
    if (property.eopid == 993)
        property.floatValue[0] = 50.0f;
    if (property.eopid == 994)
    {
        property.floatValue[0] = 50.0f;
        property.floatValue[1] = 100.0f;
        property.floatValue[2] = 80.0f;
    }
    if (property.eopid == 997)
        property.intValue = -1;
    // Defaults from InitDart, InitJlo, InitClkWork, and InitClkwrkDmg.
    if (property.eopid == 602 || property.eopid == 810 || property.eopid == 887 || property.eopid == 1141)
        property.intValue = -1;
    if (property.eopid == 891)
        property.floatValue[0] = 3000.0f;
    if (property.eopid == 892)
        property.floatValue[0] = 1000.0f;
    if (property.eopid == 1148)
        property.intValue = 16;
    if (property.eopid == 1025)
        property.intValue = -1;
    if (property.eopid == 1028)
        property.intValue = -1;
    if (property.eopid == 804)
        property.floatValue[0] = 2000.0f;
    if (property.eopid == 1041)
        property.intValue = 1;
    if (property.eopid == 1043)
        property.intValue = -1;
    if (property.eopid == 1044)
    {
        property.floatValue[0] = -0.7f;
        property.floatValue[1] = 0.7f;
    }
    if (property.eopid == 1045)
    {
        property.floatValue[0] = -1.5f;
        property.floatValue[1] = 1.5f;
    }
    if (property.eopid == 1048 || property.eopid == 1049)
        property.intValue = -1;
    if (property.eopid == 1051)
        property.floatValue[0] = 120.0f;
    if (property.eopid == 1079)
        property.intValue = 1;
    if (property.eopid == 1080 || property.eopid == 1081)
        property.floatValue[0] = 50.0f;
    if (property.eopid == 1082)
        property.floatValue[0] = 3000.0f;
    if (property.eopid == 1083)
        property.floatValue[0] = 1300.0f;
    if (property.eopid == 1084 || property.eopid == 1085 || property.eopid == 1089 || property.eopid == 1094 ||
        property.eopid == 1095 || property.eopid == 1096)
        property.intValue = -1;
    if (property.eopid == 1086 || property.eopid == 1093)
        property.intValue = 3;
    if (property.eopid == 1090 || property.eopid == 1092 || property.eopid == 1114)
        property.intValue = -1;
    if (property.eopid == 1097)
        property.floatValue[0] = 2.0f;
    if (property.eopid == 1106 || property.eopid == 1107)
        property.intValue = -1;
    if (property.eopid == 1150)
        property.intValue = 60;
    if (property.eopid == 1151)
        property.intValue = 8;
    if (property.eopid == 1155)
        property.intValue = -1;
    // InitRwm defaults. Enabling one of these overrides must preserve the
    // constructor's effective firing solution until the author changes it.
    if (property.eopid == 918 || property.eopid == 929 || property.eopid == 941 || property.eopid == 946)
        property.intValue = -1;
    if (property.eopid == 927)
        property.floatValue[0] = 5.0f;
    if (property.eopid == 936)
    {
        property.floatValue[0] = 0.0f;
        property.floatValue[1] = (std::numeric_limits<float>::max)();
    }
    if (property.eopid == 937)
    {
        property.floatValue[0] = 0.0f;
        property.floatValue[1] = 1.0f;
        property.floatValue[2] = 0.0f;
    }
    if (property.eopid == 938)
    {
        property.floatValue[0] = 0.0f;
        property.floatValue[1] = 20000.0f;
    }
    if (property.eopid == 1159 || property.eopid == 1160)
        property.intValue = -1;
    if (property.eopid == 1170)
        property.intValue = -1;
    if (property.eopid == 1173)
        property.intValue = 5;
    if (property.eopid == 201 || property.eopid == 202 || property.eopid == 210 || property.eopid == 211)
        property.boolValue = true;
    if (property.eopid == 204 || property.eopid == 205 || property.eopid == 206)
        property.intValue = -1;
    if (property.eopid == 208)
        property.floatValue[0] = 150.0f;
    if (property.eopid == 212 || property.eopid == 213 || property.eopid == 214 || property.eopid == 215 ||
        property.eopid == 218 || property.eopid == 222 || property.eopid == 223 || property.eopid == 224 ||
        property.eopid == 236 || property.eopid == 240)
        property.intValue = -1;
    if (property.eopid == 217)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 219)
    {
        property.floatValue[0] = 0.8f;
        property.floatValue[1] = 1.2f;
    }
    if (property.eopid == 220)
        property.floatValue[0] = 0.0f;
    if (property.eopid == 221)
        property.floatValue[0] = 0.0f;
    if (property.eopid == 227)
        property.intValue = 1;
    if (property.eopid == 231)
        property.intValue = 0;
    // PsfxEnsureBrk creates a complete NewSfx record when any custom break
    // sound setter is used. Match those runtime defaults for newly added fields.
    if (property.eopid == 236)
        property.intValue = -1;
    if (property.eopid == 237)
        property.floatValue[0] = 3000.0f;
    if (property.eopid == 238)
        property.floatValue[0] = 300.0f;
    if (property.eopid == 239)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 242)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 243 || property.eopid == 244)
        property.intValue = -1;
    if (property.eopid == 245 || property.eopid == 816 || property.eopid == 820)
        property.intValue = -1;
    // These come from the BRKP EOP descriptors (fDef), which the loader applies
    // even though NewBrkp itself only zero-allocates the object.
    if (property.eopid == 247)
        property.floatValue[0] = 1.5f;
    if (property.eopid == 248)
        property.floatValue[0] = 10.0f;
    // BRKP's authored default is Idle (0). Nil (-1) is only an enum sentinel;
    // using it as the reset value bypasses the normal debris lifecycle.
    if (property.eopid == 249)
        property.intValue = 0;
    if (property.eopid == 265 || property.eopid == 266 || property.eopid == 279 || property.eopid == 280 ||
        property.eopid == 894 || property.eopid == 896)
        property.intValue = -1;
    if (property.eopid == 251 || property.eopid == 252 || property.eopid == 253 || property.eopid == 254 ||
        property.eopid == 267 || property.eopid == 268 || property.eopid == 275 || property.eopid == 287 ||
        property.eopid == 289 || property.eopid == 290 || property.eopid == 291 || property.eopid == 299 ||
        property.eopid == 300)
        property.intValue = -1;
    if (property.eopid == 257 || property.eopid == 259 || property.eopid == 269 || property.eopid == 270 ||
        property.eopid == 304 || property.eopid == 305)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 263 || property.eopid == 271 || property.eopid == 274)
        property.boolValue = true;
    if (property.eopid == 286)
        property.floatValue[0] = 100.0f;
    if (property.eopid == 306)
        property.intValue = 1;
    if (property.eopid == 313 || property.eopid == 324 || property.eopid == 328 || property.eopid == 332)
        property.intValue = 0;
    if (property.eopid == 320)
        property.intValue = -1;
    if (property.eopid == 314 || property.eopid == 317)
        property.floatValue[0] = 0.25f;
    if (property.eopid == 318)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 319)
        property.floatValue[0] = 0.75f;
    if (property.eopid >= 321 && property.eopid <= 322)
        property.intValue = -1;
    if (property.eopid >= 325 && property.eopid <= 326)
        property.intValue = -1;
    if (property.eopid >= 329 && property.eopid <= 330)
        property.intValue = -1;
    if (property.eopid == 333)
        property.intValue = -1;
    if (property.eopid == 334)
        property.floatValue[0] = 8.0f;
    if (property.eopid == 335 || property.eopid == 336)
        property.floatValue[0] = 2.0f;
    if (property.eopid == 344 || property.eopid == 347 || property.eopid == 610 || property.eopid == 619 ||
        property.eopid == 620 || property.eopid == 621)
        property.intValue = -1;
    if (property.eopid == 609)
        property.intValue = 0;
    if (property.eopid == 614)
        property.floatValue[0] = 150.0f;
    if (property.eopid == 625)
        property.floatValue[0] = 100.0f;
    if (property.eopid == 640)
        property.floatValue[0] = 50.0f;
    if (property.eopid == 641)
        property.intValue = 10;
    if (property.eopid == 642)
        property.floatValue[0] = 300.0f;
    if (property.eopid == 712 || property.eopid == 713)
        property.floatValue[0] = 200.0f;
    if (property.eopid == 714 || property.eopid == 715 || property.eopid == 725)
        property.intValue = 0;
    if (property.eopid == 717 || property.eopid == 823)
        property.floatValue[0] = 800.0f;
    if (property.eopid == 718 || property.eopid == 830)
        property.floatValue[0] = 5000.0f;
    if (property.eopid == 719)
    {
        property.floatValue[0] = 50.0f;
        property.floatValue[1] = 5000.0f;
    }
    if (property.eopid == 723 || property.eopid == 724 || property.eopid == 826)
        property.boolValue = true;
    // PsfxEnsureBomb delegates to NewSfx.  Adding any one of the bomb sound
    // setters creates this complete runtime record with these exact defaults.
    if (property.eopid == 726)
        property.intValue = -1;
    if (property.eopid == 727)
        property.floatValue[0] = 3000.0f;
    if (property.eopid == 728)
        property.floatValue[0] = 300.0f;
    if (property.eopid == 729)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 752 || property.eopid == 753)
        property.floatValue[0] = 500.0f;
    if (property.eopid == 754 || property.eopid == 756)
        property.floatValue[0] = 15.0f;
    if (property.eopid == 755)
        property.floatValue[0] = 1000.0f;
    if (property.eopid == 757)
        property.floatValue[0] = 3000.0f;
    if (property.eopid == 758)
    {
        property.floatValue[0] = 0.5f;
        property.floatValue[1] = 1.0f;
    }
    if (property.eopid == 759 || property.eopid == 963)
        property.floatValue[0] = 300.0f;
    if (property.eopid == 760 || property.eopid == 761 || property.eopid == 964 || property.eopid == 950 ||
        property.eopid == 955)
        property.intValue = -1;
    if (property.eopid == 805)
    {
        property.floatValue[0] = 1.0f;
        property.floatValue[1] = 2.5f;
    }
    if (property.eopid == 806)
        property.intValue = 8;
    if (property.eopid == 807)
        property.floatValue[0] = 0.2f;
    if (property.eopid == 808)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 809)
    {
        property.floatValue[0] = 1.0f;
        property.floatValue[1] = 0.0f;
        property.floatValue[2] = 0.0f;
    }
    if (property.eopid == 829)
        property.floatValue[0] = 2000.0f;
    if (property.eopid == 833)
        property.floatValue[0] = 1500.0f;
    if (property.eopid == 834)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 948 || property.eopid == 949)
        property.intValue = 0;
    if (property.eopid == 951)
    {
        property.floatValue[0] = 200.0f;
        property.floatValue[1] = 200.0f;
    }
    if (property.eopid == 952)
    {
        property.floatValue[0] = 100.0f;
        property.floatValue[1] = 100.0f;
    }
    if (property.eopid == 953)
    {
        property.floatValue[0] = 500.0f;
        property.floatValue[1] = 500.0f;
    }
    if (property.eopid == 954)
    {
        property.floatValue[0] = -300.0f;
        property.floatValue[1] = 300.0f;
    }
    if (property.eopid == 956)
        property.floatValue[0] = 0.8f;
    if (property.eopid == 957)
    {
        property.floatValue[0] = 15.0f;
        property.floatValue[1] = 60.0f;
    }
    if (property.eopid == 958)
        property.floatValue[0] = 0.02f;
    if (property.eopid == 959)
    {
        property.floatValue[0] = 30.0f;
        property.floatValue[1] = 120.0f;
    }
    if (property.eopid == 960)
    {
        property.floatValue[0] = 0.05f;
        property.floatValue[1] = 0.2f;
    }
    if (property.eopid == 961)
    {
        property.floatValue[0] = 0.1f;
        property.floatValue[1] = 0.8f;
    }
    if (property.eopid == 962)
    {
        property.floatValue[0] = 5.0f;
        property.floatValue[1] = 15.0f;
    }
    if (property.eopid == 810 || property.eopid == 1141)
        property.intValue = -1;
    if (property.eopid == 811)
        property.floatValue[0] = 2500.0f;
    if (property.eopid == 812)
        property.floatValue[0] = 20.0f;
    if (property.eopid == 813)
        property.floatValue[0] = 30.0f;
    if (property.eopid == 814)
        property.floatValue[0] = 1.5f;
    if (property.eopid == 815)
        property.floatValue[0] = 10.0f;
    if (property.eopid == 887 || property.eopid == 602)
        property.intValue = -1;
    if (property.eopid == 891)
        property.floatValue[0] = 3000.0f;
    if (property.eopid == 892)
        property.floatValue[0] = 1000.0f;
    if (property.eopid == 1148)
        property.intValue = 16;
    if (property.eopid == 1149)
        property.intValue = 0;
    if (property.eopid == 817)
        property.intValue = 5;
    if (property.eopid == 818)
        property.floatValue[0] = 1.0f;
    if (property.eopid == 819)
        property.floatValue[0] = 20.0f;
    if (property.eopid == 1109)
        property.floatValue[0] = 1000.0f;
    if (property.eopid == 1110)
        property.floatValue[0] = 0.0f;
    if (property.eopid == 1111)
        property.floatValue[0] = 0.5f;
    if (property.eopid == 1112)
        property.floatValue[0] = 3000.0f;
    // Descriptor defaults are the final authority for EOPIDs carrying 0x800:
    // SetLoDefaults feeds these exact bytes through LoadOptionFromBrx before
    // the authored BRX option list is read.
    if (property.eopid >= 0 && static_cast<std::size_t>(property.eopid) < EditorOptionDefinitions().size())
    {
        const auto &definition = EditorOptionDefinitions()[static_cast<std::size_t>(property.eopid)];
        if (definition.runtimeDefaultable)
        {
            switch (property.kind)
            {
            case BrxPropertyKind::Bool:
                property.boolValue = definition.runtimeDefaultBits != 0;
                break;
            case BrxPropertyKind::Float:
                std::memcpy(&property.floatValue[0], &definition.runtimeDefaultBits,
                            sizeof(definition.runtimeDefaultBits));
                break;
            case BrxPropertyKind::Int32:
                property.intValue = static_cast<std::int32_t>(definition.runtimeDefaultBits);
                break;
            case BrxPropertyKind::UInt32:
                property.uintValue = definition.runtimeDefaultBits;
                break;
            case BrxPropertyKind::Int16:
            case BrxPropertyKind::TypedInt16:
                property.intValue = static_cast<std::int16_t>(definition.runtimeDefaultBits & 0xFFFFu);
                break;
            default:
                break;
            }
        }
    }
    property.modified = true;
}
