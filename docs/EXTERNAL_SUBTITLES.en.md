# External captions: sub.yaml and Todokanai

[中文说明](EXTERNAL_SUBTITLES.md)

From **0.2.70**, the same file picker also accepts original Todokanai `subtitles`
files. Select the file yourself; its filename is unrestricted and the format is
detected from its contents. Embedded captions remain the default. Both formats
require UTF-8 and have a 1 MiB limit.

```text
{
1002 9763 1002 114
0 800 First line^Second line
800 900 Next caption
}
```

The header contains audio scene, audio ID, PC script scene and script line.
The final pair is validated location metadata; playback follows actual audio
events. Each cue is `start_ms duration_ms text`, rather than start/end times.
`^` forces a line break. A later cue takes over an overlapping earlier cue.
Scene zero can match global effects; other tracks use their audio scene.
Duplicate audio identities, invalid encoding, timing and oversized documents
are rejected. The caption file supplied with UA 0.1.5 currently contains English;
you can translate its text before selecting it. The YAML rules below are unchanged.

The port uses the same field names and nesting as the main-story
`script/sub.yaml` from the previously integrated Chinese subtitle patch.
**This is not a new subtitle format.** It supports the fixed layout used by
that file, rather than every feature of general-purpose YAML.

These are supplementary captions for audio-only passages. They do not translate
ordinary story dialogue, menu graphics or character names. Translating those
still requires compatible game scripts, fonts and any necessary code changes.

## Load a file

On Android, choose **Caption source → Choose sub.yaml**, select your file using
the system file picker, wait for validation, enable extra captions and start the
game from the launcher. Embedded Chinese captions remain the default; choose the
embedded source to restore them. One caption switch controls either source.

The file can have any name, such as `sub-uk.yaml`. It can be anywhere the system
picker grants persistent read access; it does not need to be inside the APK or
replace a PAK. Its reference is saved in `native-launcher.wa2cfg` in the selected
game folder. Cancelling or selecting an invalid file preserves the old selection.
Missing files or expired access require selecting the file again.

The selected file is read and validated again on every game launch. Restart the
game after editing it; captions are not reloaded during gameplay.

Windows uses the same parser and playback implementation:

```text
WA2_SDL2_x64.exe --root <game-folder> --subtitles sub-uk.yaml
```

The subtitle path can be absolute or relative to the game folder. Omit
`--subtitles` for embedded captions. `--no-chinese-subtitles` disables the selected
source, including an external file despite the historical option name.

## Complete example

Use UTF-8, optionally with a UTF-8 BOM at the start of the file. LF and CRLF line
endings are supported. UTF-16 and legacy encodings such as GBK are not supported.
The file must be nonempty and no larger than 1 MiB (1,048,576 bytes).

This complete, parseable example uses actual IDs, timings and text from the
[original main-story file](../third_party/wa2_subtitles/sub.yaml). It contains
only two audio tracks; other tracks will have no supplementary captions:

```yaml
soundEffect:
    - id: 8365
      content:
          - begin: 0
            end: 5000
            text: 峰城广播站
voice:
    - scene: 1008
      id: 166
      content:
          - begin: 0
            end: 3000
            text: 呜哇……
```

## Fields and audio matching

| Field | Meaning |
| --- | --- |
| `soundEffect` | Sound-effect captions, matched by audio `id`. |
| `voice` | Voice captions, matched by the audio's `scene` and `id` together. |
| `scene` | Original audio scene ID, a decimal integer from 1 to 65535. |
| `id` | Audio ID, a decimal integer from 0 to 65535; not a subtitle line number. |
| `content` | The nonempty list of caption cues for this audio track. |
| `begin` | Display start in milliseconds from the start of this audio; inclusive. |
| `end` | Display end in milliseconds from the start of this audio; exclusive. |
| `text` | Nonempty, single-line caption text without enclosing quotes. |

For example, `begin: 1000` and `end: 4000` show text from 1 second until 4 seconds
after that audio starts. Each new audio track has its own time origin. These are
not timestamps for an entire movie, scene or play session. Use integer
milliseconds, not `00:01.000`, fractional values or values in seconds.

Times must satisfy `0 ≤ begin < end ≤ 4294967295`. Cues within one audio track
must be in chronological order without overlap. Gaps and touching boundaries
are allowed.

Define each sound-effect `id` once and each voice `(scene, id)` pair once.
Different voice scenes may reuse an `id`. Put multiple cues for the same audio
inside one `content` list rather than repeating the audio entry. Audio entries
themselves need not be sorted by ID; the loader sorts them.

IDs must match the audio actually triggered by the game. Reused voice clips keep
their original audio scene ID; do not substitute the current story scene ID.
When translating existing captions, normally change only `text`. New tracks need
their actual audio IDs and timings, not invented example IDs.

**Validation checks structure, ID ranges, times and available glyphs. It does not
check that an audio ID exists in your PAK files or will be triggered in a scene.**

## Multiple cues and empty groups

The next example uses an existing voice ID but illustrative text and timings to
show how two cues share one audio track. Replace them with actual content:

```yaml
soundEffect:
voice:
    - scene: 1008
      id: 166
      content:
          - begin: 0
            end: 1000
            text: First caption.
          - begin: 1000
            end: 3000
            text: Second caption.
```

Both root groups, `soundEffect:` and `voice:`, must appear exactly once. One may
be empty: leave just its heading, as with `soundEffect:` above. Do not write
`[]`, `{}` or `null`. The file needs at least one audio entry, and each entry
needs at least one valid cue.

## Exact whitespace and text syntax

Field names are case-sensitive. Follow the example's field order and use normal
ASCII spaces, not tabs:

| Start of line | Leading spaces |
| --- | --- |
| `soundEffect:`, `voice:` | 0 |
| `- id:` for an effect, or `- scene:` for a voice | 4 |
| Voice `id:`, or `content:` | 6 |
| `- begin:` | 10 |
| `end:`, `text:` | 12 |

Use one space after a colon when it has a value. Root headings and `content:`
have no value on that line. Do not let a YAML formatter change the indentation
to two spaces. Do not add trailing spaces to numeric fields or heading lines.
Leading and trailing spaces in a `text` value are trimmed.

Blank lines and full-line `#` comments are allowed; comment lines may be indented.
Inline comments are not supported: `#` after a number causes a validation error,
while `# ...` after caption text is displayed as part of that text. Put comments
on their own lines.

Each `text` occupies one physical line. The renderer wraps long captions;
the two characters `\n` are literal and do not insert a line break. Do not use
block strings (`|` or `>`), enclosing single/double quotes, YAML tags, aliases
or anchors. Quotes inside ordinary text are allowed. Text cannot start with
`! & * { } [ ] | > ' " % @` or a backtick; the parser rejects these starts.

Extra fields, duplicate fields, embedded files, `---` document markers and
Special Contents groups are not supported. SRT and ASS files cannot be selected
directly because they lack the audio scene/ID mapping used by this feature.

## Limits

| Item | Limit or requirement |
| --- | --- |
| File size | Nonempty, up to 1 MiB |
| Audio entries | 1024 total |
| Caption cues | 8192 total |
| Text per cue | 1–512 Unicode code points, not UTF-8 bytes |
| Text across all cues | 131072 code points, counting repeated text too |
| Distinct characters | 1536 across the entire file |
| Control characters | No tabs, line breaks, control characters or embedded BOM in text |

## Translate the existing captions

1. Copy the [complete original template](../third_party/wa2_subtitles/sub.yaml).
   Keep the root groups, `scene`, `id`, `begin`, `end` and indentation. Translate
   each `text` and save the file as UTF-8.
2. You may keep separate language files, such as `sub-uk.yaml` and `sub-en.yaml`.
   Choose one in the launcher. Only one source is active per run; files are not
   merged or automatically switched according to the game language.
3. Select the file, wait for validation, enable extra captions and start the
   game to check synchronization. Restart after further edits.

An external file **replaces the entire embedded caption catalogue**. Missing
tracks do not fall back to embedded Chinese. The embedded data remains intact
and can be restored by selecting the embedded source.

Caption source and game language are independent: Japanese game scripts can be
used with Ukrainian supplementary captions. This does not replace the story
script/font packs or audio. Do not add `language`, `font`, `size` or `position`
fields; the current parser does not support them.

Embedded Chinese captions and covered Chinese YAML text reuse the original atlas.
From 0.2.72, external English and Todokanai captions retain measured per-glyph
advances instead of assigning every letter, punctuation mark and space 28 pixels.
The shared layout centres by measured width, prefers word boundaries when wrapping,
and preserves explicit `^` line breaks at the same font size and line spacing.
Platform glyphs are rasterized once, then use the shared C++ layout and renderer.
Android uses system fonts;
Windows uses Microsoft YaHei and currently supports BMP characters for new
glyphs. Missing glyphs or unsupported glyph dimensions fail validation.
Complex-script shaping and complete Unicode coverage across devices are not
guaranteed. Captions use the game's aspect-preserving viewport, top-centred white
text with a black outline and automatic wrapping.

## Troubleshooting

| Message or symptom | Check |
| --- | --- |
| `sub.yaml line N: ...` | Count from the first file line, including blank lines and comments. Missing fields or timing errors may point to the start of an entry. |
| `unsupported field, YAML syntax or indentation` | Check case, fixed indentation, trailing spaces and extra fields. |
| `expected an unsigned decimal integer` | Use decimal integers without quotes, minus signs, units or inline comments. |
| `invalid or overlapping time interval` | Start must precede end; the next cue cannot start before the previous cue ends. |
| `duplicate scene/audio identity` | An audio entry is repeated; combine its cues into one `content`. |
| `text must be a literal plain string` | Check for empty text, enclosing quotes or a rejected starting character. |
| Missing glyph or unsupported font dimensions | The device font cannot produce usable glyphs, even if the structure is correct. |
| Valid file but no captions | Check the caption switch, selected source, actual audio IDs, playback times and whether the file includes this audio. |

Both platforms use the same strict parser. These examples describe the current
implementation. Memory-only component checks do not establish full-game or
device compatibility; new audio timings and patch resources still need testing
by their authors. The complete `SpecialContents/` set remains excluded from the
embedded captions. See [caption scope](SUBTITLES.md) for provenance and details.
