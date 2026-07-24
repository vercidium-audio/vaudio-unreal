# Overview

~\vaudiofps2\ThirdParty\vaudio\include\vaudio.h is the public header for Vercidium Audio.

Coding guidelines:
- Don't write "if (!something) return;" with no reasoning why 'something' could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Use DisplayWarning() to surface errors to the user on the screen. I used to use VALog() instead
- Use camelCase for variable names
- Don't use single capitalised acronyms for variable names - use position instead of P, vaWorld instead of VAW, etc.