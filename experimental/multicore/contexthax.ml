open Printf;;

type t
external self : unit -> t = "caml_context_self"
external create : (unit -> unit) -> t = "caml_context_create"
external get_id : t -> int = "caml_context_get_id"
external interrupt : t -> (unit -> unit) -> unit = "caml_context_interrupt"


let x = 1;;

let identify () = printf "I am context %d!\n" (get_id (self ()));;

let work () = identify() ; printf "working\n%!"; for i = 1 to 100000000 do () done;;


let c1 = create work;;
let c2 = create work;;
let c3 = create work;;
for i = 1 to 1000000 do () done;;

printf "I have contexts %d, %d, %d%!\n" (get_id c1) (get_id c2) (get_id c3);;
printf "Interrupting %d...%!\n" (get_id c1);;
interrupt c1 (fun () -> identify (); interrupt c2 identify);;
work ();;
