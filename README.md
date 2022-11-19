### Alun's Empire

In Europa Universalis 2, provinces convert to Protestant or Reformed instantly when the Martin Luther or John Calvin events appear. 
This is quite unrealistic, so a forum user named Alun decided to take matters into his own hands.

https://forum.paradoxplaza.com/forum/threads/aluns-reformation-mod.103112/

This tool takes a table of provinces with conversion chances and generates a series of events, by which a "dummy" country (which exists in game but owns no provinces on the map) decides whether the province will convert or not.

In EU2, the chance that an AI will pick a specific event option is as follows: 85 for the first option and 5 for each subsequent option, maximum of 4 total options. So most percentages required a sequence of events triggering each other with particular options. Because of this, the EU2 version of Empire only supported certain percentages.

In For the Glory, the ai_chance command renders such event chains obsolete. All conversion chances can be accurately modeled with a single event.

This is my update of Alun's tool for FTG.

https://forum.paradoxplaza.com/forum/threads/aluns-empire-tool-generate-reformation-events-for-your-own-mod.756288/

See [Empire_ReadMe.txt](https://raw.githubusercontent.com/mmyers/FTG_Empire/main/Empire_ReadMe.txt) for Alun's original readme and a description of how to use this tool.