# Hide Chats

Hides the Chats section of the sidebar, and its divider.

## How it works

1. The largest function that references `aside.vertical_tabs.chats_section_expanded` is the
   sidebar's constructor. The first vtable it installs is the sidebar's class.
2. After that reference, its first two stores to `[rsi+field]` are the Chats container and its
   divider.
3. Hold both hidden. The live sidebar comes from `FindObjects`, and a new window's when it first draws.

## Limits

- A new window's sidebar draws once before Chats is hidden, so Chats could flash.
- Reopening a collapsed sidebar hasn't been tested, because UI Automation couldn't re-expand it.
