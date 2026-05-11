PLACE THESE IN YOUR PROJECTS SOURCE FOLDER

Be sure to change the class in the 2 .h files.
They start with FLOP_API, yours should be whatever your project is. (Usually MYPROJECT.Build.cs you would use the MYPROJECT part)\
Replace FLOP_API with the target project/module API macro, for example MYPROJECT_API.\
• Or remove the API macro if the classes stay private to that same game module.

Commands:
dev.stat (StatToChange) (AmountToChange)\
Stamina/CurrentStamina/StaminaCurrent/MaxStamina/StaminaMax/MaximumStamina/CurrentHealth/HealthCurrent/MaxHealth/HealthMax/MaximumHealth\
dev.tp LOCATION/(leave blank it will list your teleport spots in the console window so expand it)\
dev.god on/off 

Right click and create a BP DeveloperDebugConsole, place it in your scene\
    there will be an array you can add the marker names to.(Teleport Marker Actors)\
Right click and create a BP DeveloperTeleportMarker. Name it and place in scene. Enter the same name on the debug console.

For teleport, there is an option to tick if you are using world partition, if you are, it will teleport the player to the location above your marker, wait for the scene to load, then check for ground collider then it will drop you to that spot.


How the stats to work:

It does not look for a specific player class or parent/master class.\
ex: For dev.stat, it does this:\
1.\
Gets the local player controller:\
World->GetFirstPlayerController()\
2.\
Gets that controller’s current pawn:\
PlayerController->GetPawn()\
3.\
Searches that pawn first.\
4.\
If it does not find matching stats on the pawn, it searches all components attached to that pawn.\
It uses property names, not class inheritance. It looks for numeric properties matching:\
Health\
CurrentHealth\
HealthCurrent\
MaxHealth\
HealthMax\
MaximumHealth\
or:\
Stamina\
CurrentStamina\
StaminaCurrent\
MaxStamina\
StaminaMax\
MaximumStamina\
So it works if the possessed local pawn, or one of its components, has those numeric variables. It does not care what the parent class is.
