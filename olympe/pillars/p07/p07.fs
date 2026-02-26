\ Pillar p07 (GHOST-G) — Forth scaffold

: p07-ok ( -- f ) true ;

: p07-assert ( f -- )
  0= IF
    ." p07 forth: FAIL" cr
    -1 throw
  THEN
;

p07-ok p07-assert
." p07 forth: OK" cr
