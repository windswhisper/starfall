# Manual Writing Guide

The Markdown manual is an English reference for mod authors, AI assistants, and maintainers. Write clear, concise instructions based on the current implementation.

## Organization

- Keep `docs/manual.md` as the main entry with task links and a directory of topic guides.
- Organize each topic around its purpose, a usable starting example, configuration or API details, and relevant behavior and limits.
- Document common fields together. Put fields specific to an effect or behavior in its own section.
- Keep custom superweapons and custom ground pickups in the miscellaneous guide. Keep active skills in their own guide. Keep map triggers and team scripts in the map guide.
- Preserve existing links and anchors when moving material. Link to the authoritative explanation instead of duplicating detailed rules.

## INI fields

Present INI parameters in a fenced `ini` block, grouped under the section where they are configured. Put the field and its value type first, followed by a comment explaining its purpose. Put defaults and constraints on another comment line when applicable. Leave a blank line between fields.

```ini
[SectionID]
FieldName=(value type)
; Explains what the field controls.
; Default: value; range: accepted values

OtherField=(value type)
; Explains the second field.
```

Use actual field names, types, defaults, units, and constraints verified against the implementation. Label placeholders and partial configuration examples clearly. Put each alternative trigger, action, or effect with its own explanation and identify alternatives as such. Tables may describe comparisons, API methods, selectors, or troubleshooting.

## Verification

Check examples against the supported configuration and relevant code. Confirm relative links, moved anchors, code fences, and links between guides. Update the main entry when adding or moving a topic.
