# Virtual-MoCap
MocapRecorder is an Unreal Engine 5 editor tool that records **skeletal animation** from actors in your level and then **bakes** those recordings into Animation Sequence assets (UAnimSequence) through an editor-side bake queue.
Disclaimer: This tool used AI heavily during its creation, However AI is not being used in the tool itself.

MocapRecorder Plugin — Description + Usage Guide
=================================================

What this plugin is
-------------------
MocapRecorder is an Unreal Engine 5 editor tool that records **skeletal animation** from actors in your level and then **bakes** those recordings into Animation Sequence assets (UAnimSequence) through an editor-side bake queue.

It is designed for:
- Recording multiple characters/actors in one take (multi-capture).
- Keeping the workflow “one-click” inside the editor (no external capture tools required).
- Handling “spawned actor” cases (bullets, casings, spawned limbs, VFX actors, etc.) via **Class / Instance Capture Rules**.

Important notes (current behavior)
----------------------------------
- **Skeletal meshes are required. For all captured Actors, you must have a skeletal mesh for every unique actor.** The project direction is skeletal-only capture.
- A legacy enum value for “Transform Only (Deprecated)” exists only to avoid breaking older serialized assets, but the intended workflow is skeletal capture.

Modules
-------
- MocapRecorder (runtime component)
- MocapRecorderEditor (editor UI, session manager, bake pipeline)

----------------------------------------------------------------------
INSTALLATION (step-by-step)
----------------------------------------------------------------------

##For BP only projects there is an Option B below Option A with the step by step instructions for your use case##

Option A — Install as a *Project Plugin*
1) Close Unreal Editor.
2) In your UE project folder, create this folder if it doesn’t exist:
   <YourProject>/Plugins/
3) Copy the entire plugin folder into:
   <YourProject>/Plugins/MocapRecorder/
   If the /Plugins? folder doesn't exist make one.
4) Right-click your .uproject file → “Generate Visual Studio project files”.
5) Open the project in Visual Studio (or Rider) and build the Editor target:
   - Configuration: Development Editor
   - Platform: Win64 (or your platform)
   -Editor Target = Your project folder not the entire solution. (to be sure just right click the YourProjectHere and select build. in the list on the right side of visual studios )
6) Launch Unreal Editor.
7) Go to:
   Edit → Plugins → (search) “MocapRecorder”
   Enable it if it’s not enabled.
8) Restart the editor if prompted.

----------------------------------------------------------------------
OPENING THE TOOL (3 ways)
----------------------------------------------------------------------

Once installed/enabled, you can open the panel in any of these ways:

1) Main Menu:
   Window → Mocap Recorder → Open Mocap Recorder

2) Toolbar:
   A toolbar button labeled **“Mocap”** opens the window.

3) Right-click menus in the level:
   - Right-click in the Level Viewport → Mocap Recorder → Open Mocap Recorder
   - Right-click an Actor in the Outliner/Level → Mocap Recorder → Open Mocap Recorder

OPTION B — Blueprint-Only Projects (Full Copy‑Paste Section)
===================================================

This section is intended for **Blueprint‑only Unreal Engine projects**
(projects that were created without any C++ code).

Because MocapRecorder is a **C++ plugin**, Blueprint‑only projects require
one extra conversion step so Unreal can compile the plugin.

----------------------------------------------------------------------
WHAT “Blueprint‑Only” MEANS IN PRACTICE
----------------------------------------------------------------------

If your project:
- Was created using the **Blueprint** template
- Does **not** have a Source/ folder
- Does **not** open Visual Studio / Rider when building

Then Unreal has no native C++ build target yet.
We must add one **once**. After that, you can continue working purely in Blueprints.

----------------------------------------------------------------------
STEP‑BY‑STEP INSTALLATION (Blueprint‑Only Projects)
----------------------------------------------------------------------

STEP 1 — Close Unreal Editor
----------------------------
Make sure Unreal Editor is completely closed before starting.

----------------------------------------------------------------------
STEP 2 — Install the Plugin into the Project
--------------------------------------------

1) Navigate to your project folder on disk.

2) Create a folder called:
   Plugins

   Example:
   <YourProject>/Plugins/

3) Copy the entire MocapRecorder plugin folder into:
   <YourProject>/Plugins/MocapRecorder/

   The final structure should look like:

   <YourProject>
   ├─ Content/
   ├─ Plugins/
   │  └─ MocapRecorder/
   │     ├─ Source/
   │     ├─ Resources/
   │     ├─ MocapRecorder.uplugin
   │     └─ ...
   ├─ Config/
   └─ YourProject.uproject

----------------------------------------------------------------------
STEP 3 — Convert the Project to Support C++ (ONE‑TIME)
-------------------------------------------------------

This does NOT mean you must write C++ later.
It only enables plugin compilation.

1) Right‑click your .uproject file.
2) Select:
   “Generate Visual Studio project files”

   (If you do not see this option, open the .uproject once in Unreal,
    then close it and try again.)

3) Double‑click the .uproject file to open Unreal Editor.

4) Unreal will display a message similar to:
   “Missing modules. Would you like to rebuild them?”

5) Click:
   **Yes**

Unreal will now:
- Create a Source/ folder
- Create a minimal C++ module
- Open Visual Studio (or Rider)
- Compile the project and plugin

----------------------------------------------------------------------
STEP 4 — Build the Editor (First Time Only)
-------------------------------------------

If Visual Studio opens:

1) In the toolbar:
   - Configuration: **Development Editor**
   - Platform: **Win64** (or your platform)

2) Click:
   **Build**

3) Wait for the build to finish successfully.

This step may take several minutes the first time.

----------------------------------------------------------------------
STEP 5 — Launch Unreal Editor
-----------------------------

Once the build completes:
- Unreal Editor should launch automatically
  OR
- Re‑open the .uproject file manually

If prompted about enabling plugins:
- Enable **MocapRecorder**
- Restart the editor if asked

----------------------------------------------------------------------
STEP 6 — Verify Plugin Is Enabled
---------------------------------

Inside Unreal Editor:

1) Go to:
   Edit → Plugins
2) Search for:
   **MocapRecorder**
3) Ensure it is:
   ✔ Enabled
4) Restart the editor if required

----------------------------------------------------------------------
QUICK START (manual recording of selected actors)
----------------------------------------------------------------------

Goal: record one or more existing actors you manually select.

1) In the World Outliner, select the actor(s) you want to record.
   - These should be actors that have a **SkeletalMeshComponent** (characters, creatures, etc.). The default BP_Character or BP_ThirdPersonCharacter are fully compatible.
   -Static meshes that are not Spawned in after enabling PIE mode will need the Mocap Recorder Component added to the BluePrint. Simply find your character blueprint open it in the editor and at the top left click the Green button that says add and then search for Mocap Recorder select it from the list. Leave it as a default name. Ensure the mocap recorder component is on the same hierachical level as the scene root. 
2) Open the Mocap Recorder window. If you do not see it on launch of the editor, right click in the viewport and near the bottom of the pop up there will be an option to open mocap recorder. If you do not see it, ensure the plugin is enabled in the plugins folder. If you don't see it in there repeat the installation steps and try again. If you still can't get it to work send me an email containing your projects log file contained in Yourproject/saved/logs/Latest.lop @virtualmocapofficial@gmail.com
3) Click **Add Selected Actors**
4) Verify the actors appear under **Capture Targets**.
5) Optional: set your rates:
   - **Capture Hz** (how often the recorder samples during recording)
   - **Export FPS** (frame rate used when baking the final AnimSequence)
6) Click **REC** to start the session.
7) Play your scene (PIE) / run the action you want.
8) Click **STOP** to end the session.
9) The **Bake Queue** will process and create/bake animation assets.
	- The bake queue will not begin baking while PIE mode is still active. After disabling PIE the baking will begin automatically.

----------------------------------------------------------------------
COMMON QUESTIONS (Blueprint‑Only Users)
----------------------------------------------------------------------

Q: Do I need to write C++ now?
A: No. The C++ module exists only to allow Unreal to compile the plugin.

Q: Can I delete the Source/ folder later?
A: No. The project now depends on it for plugin compilation.

Q: Will this break my Blueprint workflow?
A: No. Blueprint usage is unchanged.

Q: Why is this necessary?
A: Unreal cannot compile C++ plugins inside a Blueprint‑only project
   unless the project has a native build target.

----------------------------------------------------------------------
TROUBLESHOOTING
----------------------------------------------------------------------

Problem: “Missing modules” error loop
- Make sure Visual Studio Build Tools are installed
- Ensure you are building **Development Editor**
- Regenerate project files again and rebuild

Problem: Plugin does not appear in Plugins list
- Confirm folder path is exactly:
  <YourProject>/Plugins/MocapRecorder/
- Confirm MocapRecorder.uplugin exists

----------------------------------------------------------------------
END — OPTION B (Blueprint‑Only Projects)
----------------------------------------------------------------------



----------------------------------------------------------------------
UI REFERENCE — buttons, toggles, and when to use them
----------------------------------------------------------------------

A) Top Row Controls
-------------------

1) “Add Selected Actors”
- What it does:
  Adds the currently selected actors (from the Outliner) to the Capture Targets list.
- When to use it:
  Use this before recording to define what gets captured in your take.
  This is also commonly used for any actors that are not generated or spawned on game start. For example a static building that has animations like sliding doors.
- Tips:
  If you add the wrong things, use **Clear** to reset and re-add.

2) “Clear”
- What it does:
  Clears the current Capture Targets list (manual targets).
- When to use it:
  Starting a new take, or if your target list has stale actors, or any actors no longer needed in the current take.

3) “REC”
- What it does:
  Starts the recording session. (Enabled only when in PIE mode.)
- When to use it:
  Right before you run the gameplay/cinematic action you want to capture.

4) “STOP”
- What it does:
  Stops the recording session. And adds the captured animation to the Bake Queue. The bake queue will not bake until PIE has been ended. Which allows for taking multiple takes in a single play session.
- When to use it:
  When the action is finished and you want to bake/export the captured frames.

B) Settings
-----------

1) “Capture Hz” (numeric)
- What it does:
  Controls the sampling rate used while recording (samples per second).
- Common values:
  - 60 for general animation capture
  - 90/120 for higher fidelity motion (more data, larger memory/asset size)
- When to raise it:
  Fast motion, detailed combat, quick hits, rapid limb motion.
- When to lower it:
  Long sessions, slower motion, or if you’re hitting performance/memory limits.

2) “Export FPS” (numeric)
- What it does:
  Controls the baked animation’s frame rate (the rate written into the AnimSequence).
- Common values:
  - 30 for standard playback / lighter files
  - 60 for higher fidelity playback or if your pipeline expects 60fps anims
- Typical workflow:
  Capture at 60–120 Hz, export at 30–60 FPS depending on need.

C) Bake Queue
-------------

This section shows the status of animation baking jobs.

1) “Clear Bake Queue”
- What it does:
  Cancels/clears pending bake jobs.
- When to use it:
  If you recorded something wrong and don’t want the current queued bakes,
  or if you want to reset after a failed/aborted bake.

2) Progress bar + status line
- What it shows:
  - “Baking X/Y (Remaining: Z) | Current: …”
  - It can also show “Waiting for UE async compile…” if the editor is busy compiling assets.
- When it matters:
  If you do multi-capture, several actors may enqueue bakes. This section is your “is it still working?” indicator.

D) Capture Targets (manual targets list)
----------------------------------------

This is the list of actors you explicitly told the tool to record.

Each row has:

1) Enable checkbox (per target)
- What it does:
  If checked, that actor will be recorded when you press REC.
- When to use it:
  Temporarily exclude an actor without removing your whole target list.
  Example: you have 5 actors but want to test just 1.

2) Target label
- What it does:
  Displays the target’s name/label (the UI keeps a “last known label” so you can spot stale entries).
- When to use it:
  Use it to confirm you’re recording the right actors.

E) Class / Instance Capture Rules (spawned actor capture)
---------------------------------------------------------

This panel is specifically for “record anything that spawns matching these rules”.

Use-cases:
- Bullets/casings spawned during firing.
- Spawned enemy limbs or ragdoll actors.
- Temporary spawned actors that do not exist in the Outliner at the start of the take.

Top buttons:
1) “Add Rule”
- Adds a new rule row.

2) “Clear Rules”
- Clears all rules.

Each rule has:

Rule Row 1 (what to match)
1) Enabled checkbox
- Turns the rule on/off.

2) Class picker
- Choose the Blueprint/Class you want to capture automatically.
- Example:
  BP_Bullet, BP_Casing, BP_SpawnedEnemy, etc.

3) “Tag” text field (optional)
- If set, the spawned actor must have this Actor Tag to match the rule.
- When to use it:
  When multiple spawned actors share a class, but you only want a subset.
- Leave empty if you want “all of that class”.

4) “Remove”
- Removes this rule row.

Rule Row 2 (auto-stop policies: stationary + radius)
1) “Stop if stationary” (toggle)
- If enabled, the recorder will auto-stop a spawned instance when it becomes nearly stationary.
- Good for:
  Bullets/casings that come to rest.
  Debris that settles.
- Be careful with:
  Anything that pauses briefly mid-flight; tune thresholds if needed.
  Or anything that intentionally comes to a stop before moving again. say an ambush predator.

2) “Speed” (numeric threshold)
- Linear speed threshold that counts as “nearly stationary”.
- Lower = stricter (must be very still).
- Higher = stops sooner.

3) “Hold(s)” (numeric seconds)
- How long the actor must stay below the speed threshold before stopping.
- Prevents stopping due to brief slowdowns.

4) “Stop outside player radius” (toggle)
- If enabled, the instance stops recording once it leaves a radius around the player.
- Good for:
  Projectiles that fly far away and you don’t care about after they leave the scene.

5) “Radius” (numeric)
- The radius used by the above toggle.

Rule Row 3 (auto-stop policies: events + bake)
1) “Stop on Hit” (toggle)
- Stops recording the instance when a hit event is detected.
- Good for:
  Bullets that impact something and you only care up to impact.

2) “Stop on Destroyed” (toggle)
- Stops recording when the actor is destroyed.
- Good for:
  Short-lived spawned actors that self-destruct after impact or timeout. Grenades or similar thown objects.

3) “Auto-bake on stop” (toggle)
- If enabled, when the instance auto-stops it will automatically enqueue a bake job.
- Good for:
  Hands-off capture of lots of bullets/casings.
- If disabled:
  Useful when testing rule matching and you don’t want a flood of baked assets.

----------------------------------------------------------------------
WORKFLOWS (practical recommendations)
----------------------------------------------------------------------

1) Recording a fight scene with 2–5 characters (this also functions in multiplayer and singular multicapture)
- Add all characters via “Add Selected Actors”.
- Capture Hz: 60–120
- Export FPS: 30 or 60
- Use per-target enable checkboxes to exclude extras for quick iteration.
- Record with REC/STOP, then wait for the Bake Queue to finish.

2) Capturing bullets/casings during combat (spawned instances)
- Create a Class Rule for BP_Bullet / BP_Casing.
- Start with:
  - Stop on Destroyed = ON
  - Auto-bake on stop = ON
- If bullets persist:
  use Stop on Hit or Stop if stationary.
- If bullets fly far away:
  enable Stop outside player radius.

3) Performance sanity
- If the editor slows down:
  Lower Capture Hz, reduce number of targets, or disable Auto-bake and bake manually in smaller sets.
- If baking seems “stuck”:
  Check Bake Queue status text for “Waiting for UE async compile…”.

----------------------------------------------------------------------
TROUBLESHOOTING (common issues)
----------------------------------------------------------------------

1) “REC” does nothing / can’t start session
- Make sure you added at least one valid target with a SkeletalMeshComponent.
- Make sure PIE/world is valid and the tool window is open.
- If you changed levels/worlds, re-open the panel and re-add actors.

2) Targets show up but nothing bakes
- STOP ends the session; baking occurs via the bake queue after STOP.
- Watch “Bake Queue” status until it returns to “Idle”.

3) Spawned actors aren’t being captured by rules
- Confirm the class picker is set to the correct BP/Class.
- If you used a Tag filter, confirm the spawned actor actually has that Actor Tag.
- Ensure the rule is enabled (checkbox).

4) Too many baked assets created (flood)
- Disable “Auto-bake on stop” on the rule while you tune matching/stop behavior.

FBX EXPORT — STEP-BY-STEP (FULL COPY-PASTE)
===========================================

This section explains exactly how to export FBX files using the MocapRecorder plugin,
from a completed recording through a usable FBX on disk.

This assumes:
- You already recorded a session using REC / STOP
- Animation assets (UAnimSequence) were successfully baked
- You want to export those animations to FBX for Blender, Maya, or other DCC tools

----------------------------------------------------------------------
OVERVIEW OF THE EXPORT PIPELINE
----------------------------------------------------------------------

The MocapRecorder plugin works in three distinct stages:

1) Record motion → stored internally as sampled frames
2) Bake → converts recorded frames into UAnimSequence assets
3) Export → converts UAnimSequence assets into FBX files

FBX export is always performed from Animation Sequences, not directly from live recording.

----------------------------------------------------------------------
STEP-BY-STEP: EXPORTING FBX FILES
----------------------------------------------------------------------

STEP 1 — Verify the Bake Completed
----------------------------------

1) After pressing STOP, wait for the Bake Queue to finish.
2) The status line should return to an idle state.
3) Open the Content Drawer and navigate to your bake folder.
4) Confirm that AnimSequence assets exist.

If no AnimSequence assets exist:
- FBX export will not work.
- Fix baking issues first.

----------------------------------------------------------------------
STEP 2 — Locate the Animation Sequence(s)
-----------------------------------------

1) Open the Content Drawer.
2) Navigate to the folder containing baked animations. Should be a folder called MocapCaptures
3) Select one or more AnimSequence assets. Highly recommend exporting all at once.

Notes:
- Multi-capture produces one AnimSequence per actor.
- Spawned instances may also produce AnimSequences.

----------------------------------------------------------------------
STEP 3 — Right-Click Export
---------------------------

1) Right-click the selected AnimSequence asset(s).
2) Choose:
   Asset Actions → Export…

This uses Unreal Engine’s native FBX exporter.

----------------------------------------------------------------------
STEP 4 — Choose Export Location
-------------------------------

1) Choose a destination folder on disk.
2) Click Save.

For multiple selections:
- One FBX will be generated per AnimSequence.

----------------------------------------------------------------------
STEP 5 — FBX Export Options (RECOMMENDED)
----------------------------------------

Animation
- Export Animation: ON
- Export Local Time: ON
- Export Preview Mesh: OFF
- Export Rigid Mesh: OFF

Mesh
- Export Skeletal Mesh: ON
- Export Static Mesh: ON If available

Transform
- Convert Scene: ON
- Force Front XAxis: OFF

Axis Conversion
- Forward Axis: -Y
- Up Axis: Z

Advanced
- Export Morph Targets: OFF
- Export Custom Attributes: OFF
- Level of Detail: OFF

----------------------------------------------------------------------
STEP 6 — Complete the Export
----------------------------

1) Click Export.
2) Wait for completion.
3) Verify the FBX file exists and has non-zero size.

----------------------------------------------------------------------
BATCH EXPORT (OPTIONAL)
----------------------------------------------------------------------

1) Select multiple AnimSequences.
2) Right-click → Asset Actions → Export…
3) Choose a folder.
4) Unreal exports one FBX per animation.

----------------------------------------------------------------------
BLENDER IMPORT REFERENCE
----------------------------------------------------------------------

When importing into Blender:

Highly recommend using a multi importer, https://github.com/Tilapiatsu/blender-Universal_Multi_Importer

- File → Import → FBX
- Scale: 1.0
- Apply Transform: ON
- Automatic Bone Orientation: on
- Use Pre/Post Rotations: ON

----------------------------------------------------------------------
IMPORTANT NOTES
----------------------------------------------------------------------

- MocapRecorder does not directly export FBX.
- Unreal’s native exporter is used by design.

----------------------------------------------------------------------
END — FBX EXPORT INSTRUCTIONS
----------------------------------------------------------------------

