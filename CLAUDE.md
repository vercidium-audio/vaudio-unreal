# Overview

~\vaudiodemo\ThirdParty\vaudio\include\vaudio.h is the public header for Vercidium Audio.

Coding guidelines:
- Don't write "if (!something) return;" with no reasoning why 'something' could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Use VALog() instead of directly calling VaRawLog or UE_LOG. VALog() automatically appends the name of the current object (emitter, world, etc), file name and function name
- Warnings / configuration issues should be clearly and directly exposed to the user
- Use camelCase for variable names
- Don't use single capitalised acronyms for variable names - use position instead of P, vaWorld instead of VAW, etc.

# TODO

- Use a Data Asset instead of actors for VAudioMaterial. Ensure fields can still be updated at runtime in the editor, for previewing material changes
- New VAudioRelativeSource.cpp class/actor - for sounds that play at the main listener emitter (footsteps, gunshots, etc). No need for an emitter - always clear, use reverb from the main listener emitter
- New VAudioAmbientSource.cpp class/actor - for ambient rain/wind sounds. No need for an emitter - muffle based on the main listener emitter's ambientFilter
- Edit VAudioMaterial.cpp - need to support custom materials. Refer to how vaudio-godot-openal\nodes\VAMaterial.cs does it - IsDefault checkbox - when enabled, have dropdown for 23 default materials. When disabled, have a number field for material ID (>= 1000) and string field for material name. Need to be able to select custom materials in the VAudioMaterialComponent.cpp too. 