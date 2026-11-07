#!/usr/bin/env python3
"""Create an opt-in OpenBFME fishing mod. All definitions here are newly authored.
Models, textures, sounds, movement and the hull are inherited at runtime from the
user's installed game. This is an OpenBFME extension, not a retail BFME2 mod.
"""
import argparse
from pathlib import Path

DEFINITIONS = """; OpenBFME optional fishing gameplay. Authored definitions, no bundled assets.
ChildObject OpenBFMEFishingBoat ElvenTransportShip
  DisplayName = OBJECT:OpenBFMEFishingBoat
  BuildCost = 300
  BuildTime = 20
  CommandPoints = 5
  CommandSet = OpenBFMEFishingBoatCommandSet
  RemoveModule ModuleTag_Contain
  Behavior = OpenBFMEFishingUpdate ModuleTag_Fishing
    TargetFilter = ANY +OpenBFMEFishingSpot
    Range = 180
    IncomeInterval = 5000
    IncomeAmount = 25
  End
End

ChildObject OpenBFMEFishingSpot TropicalFish
  DisplayName = OBJECT:OpenBFMEFishingSpot
  Scale = 2.0
  RemoveModule ModuleTag_DefaultInactiveBody
  Body = ActiveBody ModuleTag_FishingSpotHealth
    MaxHealth = 1000
  End
  KindOf = PRELOAD SELECTABLE IMMOBILE NOT_AUTOACQUIRABLE NO_BASE_CAPTURE
  RemoveModule ModuleTagWanderAround
  Behavior = AIUpdateInterface ModuleTag_StationaryFish
  End
  LocomotorSet
    Locomotor = TransportShipLocomotor
    Condition = SET_NORMAL
    Speed = 0
  End
  Geometry = CYLINDER
  GeometryMajorRadius = 35
  GeometryHeight = 25
  GeometryIsSmall = No
End

CommandSet OpenBFMEFishingBoatCommandSet
  14 = Command_Stop
End

CommandButton Command_ConstructOpenBFMEFishingBoat
  Command = UNIT_BUILD
  Object = OpenBFMEFishingBoat
  Options = CANCELABLE
  TextLabel = CONTROLBAR:OpenBFMEFishingBoat
  DescriptLabel = CONTROLBAR:OpenBFMEFishingBoatHelp
  ButtonImage = BCShipwright_GoodTroopship
  ButtonBorderType = BUILD
  Radial = Yes
  InPalantir = Yes
  ShowProductionCount = Yes
End

CommandSet GoodPortCommandSet
  6 = Command_ConstructOpenBFMEFishingBoat
End
CommandSet EvilPortCommandSet
  6 = Command_ConstructOpenBFMEFishingBoat
End
CommandSet ShipWrightCommandSet
  4 = Command_ConstructOpenBFMEFishingBoat
End
"""

STRINGS = {
    "OBJECT:OpenBFMEFishingBoat": "Fishing Ship",
    "OBJECT:OpenBFMEFishingSpot": "Fishing Spot",
    "CONTROLBAR:OpenBFMEFishingBoat": "Build Fishing Ship",
    "CONTROLBAR:OpenBFMEFishingBoatHelp": "Right-click a fishing spot to fish. Earns 25 resources every 5 seconds while fishing. Move or Stop cancels fishing.",
}

def create_mod(destination):
    root = Path(destination)
    ini = root / "data/ini/openbfme_fishing.ini"
    ini.parent.mkdir(parents=True, exist_ok=True)
    ini.write_text(DEFINITIONS, encoding="utf-8")
    placement = root / "data/ini/openbfme_fishing_placement.ini"
    placement.write_text("""FishingPlacement
  Template = OpenBFMEFishingSpot
  MinimumDepth = 20
  ShoreClearance = 120
  Spacing = 650
  ScanStep = 120
  MaximumSpots = 64
End
""", encoding="utf-8")
    # A separate mod string file is discovered by the game's string-table loader.
    strings = root / "data/openbfme_fishing.str"
    strings.write_text("\n".join(f'{key}\n"{value}"\nEND\n' for key, value in STRINGS.items()), encoding="utf-8")
    return root

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    print(create_mod(args.destination))
