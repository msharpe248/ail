" Vim syntax file
" Language:     AIL Environment Config (.ail.env)
" Maintainer:   AIL Contributors
" Filenames:    *.ail.env

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syn match   ailEnvComment  "#.*$" contains=@Spell
syn keyword ailEnvSecret   secret
syn match   ailEnvKind     "\<\(llm\|s1\|embedder\|db\|vector_db\|mcp\|skills\)\." nextgroup=ailEnvBinding
syn match   ailEnvBinding  "[a-zA-Z_][a-zA-Z0-9_-]*" contained
syn match   ailEnvVar      contained "\${[A-Za-z_][A-Za-z0-9_]*}"
syn match   ailEnvEscape   contained "\\[\"\\/bfnrt]"
syn region  ailEnvString   start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=ailEnvEscape,ailEnvVar
syn match   ailEnvNumber   "\<-\?\d\+\(\.\d\+\)\?\([eE][+-]\?\d\+\)\?\>"
syn keyword ailEnvBool     true false
syn keyword ailEnvNull     null

hi def link ailEnvComment  Comment
hi def link ailEnvSecret   PreProc
hi def link ailEnvKind     Type
hi def link ailEnvBinding  Identifier
hi def link ailEnvVar      Special
hi def link ailEnvEscape   SpecialChar
hi def link ailEnvString   String
hi def link ailEnvNumber   Number
hi def link ailEnvBool     Boolean
hi def link ailEnvNull     Constant

let b:current_syntax = "ailenv"

let &cpo = s:cpo_save
unlet s:cpo_save
