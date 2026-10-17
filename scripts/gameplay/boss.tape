# The tier 1 boss (round 10): attack, a spell, defend, then auto-battle until the end of the fight.
# Rounds 1-9 play hidden with --no-anim, then the run is saved and resumed with animation.
Output docs/media/boss.gif
Source scripts/gameplay/settings.tape

Hide
Type "clear && rpg-golang --seed 42 --lang en --no-anim"
Enter
Sleep 1.5s
Source scripts/gameplay/new-run.tape
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "0"
Sleep 500ms
Type "53"
Sleep 500ms
Type "q"
Sleep 500ms
Type "0"
Sleep 1s
Type "clear && rpg-golang --seed 42 --lang en"
Enter
Sleep 1.5s
Type "1"
Sleep 1s
Show

Sleep 1s
Type "0"
Sleep 3s
Type "1"
Sleep 2s
Type "2"
Sleep 2s
Type "1"
Sleep 2.5s
Type "4"
Sleep 2.5s
Type "5"
Sleep 1.5s
Type "3"
Sleep 15s
