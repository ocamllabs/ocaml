open Printf;;

external globalize : Obj.t -> Obj.t = "caml_obj_globalize"
external canonicalize : Obj.t -> Obj.t = "caml_obj_canonicalize"


let x = [1,2,ref [3]; 4,5,ref [6]]
and x' = [1,2,ref [3]; 4,5,ref [6]]
let x1 = x
and x2 = x
let y = Obj.obj (globalize (Obj.repr x));;
let z = Obj.obj (globalize (Obj.repr x));;



match x1 with 
    [a,b,{contents = [c]};d,e,{contents = [f]}] -> printf "x1: %d %d %d %d %d %d\n" a b c d e f
  | _ -> failwith "wat";;


printf "%!";;

printf "%b %b\n" (x = x') (x == x');;
printf "%b %b\n" (x = y) (x == y);;
printf "%b %b\n" (z = y) (z == y);;


match y with 
    [a,b,{contents = [c]};d,e,{contents = [f]}] -> printf "%d %d %d %d %d %d\n" a b c d e f
  | _ -> failwith "wat";;



printf "%!";;


match x with
    (_,_,r)::_ -> r := [42]
  | _ -> failwith "wat";;


printf "%!";;


match y with 
    [a,b,{contents = [c]};d,e,{contents = [f]}] -> printf "%d %d %d %d %d %d\n" a b c d e f
  | _ -> failwith "wat";;




printf "%!";;


(* printf "%d\n%!" (String.length (Obj.marshal (Obj.repr x)));;   *)

Gc.minor () ;;


Gc.minor () ;;

(* printf "%d\n%!" (String.length (Obj.marshal (Obj.repr x)));; *)


match x1 with 
    [a,b,{contents = [c]};d,e,{contents = [f]}] -> printf "x1: %d %d %d %d %d %d\n" a b c d e f
  | _ -> failwith "wat";;


printf "%!";;

