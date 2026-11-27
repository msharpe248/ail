" indent/ail.vim — Auto-indentation for AIL (Ruby-inspired blocks)

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal indentexpr=GetAilIndent()
setlocal indentkeys=0{,0},0),0],!^F,o,O,e,=end,=elsif,=else,=when,=rescue,=ensure

if exists("*GetAilIndent")
  finish
endif

let s:open_re = '^\s*\(def\|class\|module\|schema\|tool\|guard\|skill\|decide\|agent\|flow\|within\|retry_up_to\|mcp_server\|a2a_agent\|if\|elsif\|else\|unless\|case\|when\|while\|until\|begin\|rescue\|ensure\)\>'
let s:do_re   = '\<do\>\(\s*|[^|]*|\)\?\s*\(#.*\)\?$'
let s:mid_re  = '^\s*\(elsif\|else\|when\|rescue\|ensure\)\>'
let s:end_re  = '^\s*end\>'

function! GetAilIndent() abort
  let lnum = prevnonblank(v:lnum - 1)
  if lnum == 0
    return 0
  endif

  let prevline = getline(lnum)
  let curline  = getline(v:lnum)
  let ind      = indent(lnum)

  " Increase indent after block openers, `do`, or open brackets at end of line
  if (prevline =~# s:open_re || prevline =~# s:do_re) && prevline !~# '\<end\>\s*\(#.*\)\?$'
    let ind += shiftwidth()
  elseif prevline =~# '[\[{]\s*\(#.*\)\?$'
    let ind += shiftwidth()
  endif

  " Dedent on closing keywords / mid-block keywords / closing brackets
  if curline =~# s:end_re || curline =~# s:mid_re || curline =~# '^\s*[\]}]'
    let ind -= shiftwidth()
  endif

  return ind < 0 ? 0 : ind
endfunction
