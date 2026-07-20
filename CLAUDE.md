# Overview

~\vaudiodemo\ThirdParty\vaudio\include\vaudio.h is the public header for Vercidium Audio.

Coding guidelines:
- Don't write "if (!something) return;" with no reasoning why 'something' could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Use VALog() instead of directly calling VaRawLog or UE_LOG. VALog() automatically appends the name of the current object (emitter, world, etc), file name and function name
- Warnings / configuration issues should be clearly and directly exposed to the user
- Use camelCase for variable names
- Don't use single capitalised acronyms for variable names - use position instead of P, vaWorld instead of VAW, etc.

# TODO

- New VAudioRelativeSource.cpp class/actor - for sounds that play at the main listener emitter (footsteps, gunshots, etc). No need for an emitter - always clear, use reverb from the main listener emitter
- New VAudioAmbientSource.cpp class/actor - for ambient rain/wind sounds. No need for an emitter - muffle based on the main listener emitter's ambientFilter