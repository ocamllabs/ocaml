(* Various simple tests, all should pass. Whenever an error is discovered,
   please add here.
*)

(* From Problems.txt  Wed Oct  2 08:39:04 BST 2002
  Occurrences of a csp value share the same instantiated type
*)

let f1 x = x;;                             (* polymophic *)
let t1 = .<(f1 1, f1 true)>.;;
(*
val t1 : ('cl, int * bool) code = .<
  (((((* cross-stage persistent value (id: f1) *)) 1)),
   ((((* cross-stage persistent value (id: f1) *)) true)))>.
*)
let (1,true) = .! t1;;

(* From Problems.txt Thu Oct 24 09:55:36 BST 2002
  CSP at level n+2 gives Segmentation fault
*)
(* use f1 above *)
let t2 = .<(.<f1 1>., .<f1 true>.)>.;;
(*
val t2 : ('cl, ('cl0, int) code * ('cl1, bool) code) code = .<
  ((.<(((* cross-stage persistent value (id: f1) *)) 1)>.),
   (.<(((* cross-stage persistent value (id: f1) *)) true)>.))>.
*)
let 1 = .! (fst (.! t2));;
let true = .! (snd (.! t2));;

(* From Problems.txt Mon Nov 25 10:10:32 GMT 2002
  CSP of array ops gives internal errors
*)

let t3 = .<Array.get [|1|] 0>.;;
(*
val t3 : ('cl, int) code = .<([|1|]).(0)>. 
*)
let 1 = .! t3;;

(* From Problems.txt Tue Jan 20 12:18:00 GMTST 2004
  typechecker broken for csp ids, e.g. we get the wrong type
  We get the incorrect typing (inner and outer code forced to be both 'a)
   # .<fun x -> .<x>.>.;;
*)

let t4 = .<fun x -> .<x>.>.;;
(*
val t4 : ('cl, 'a -> ('cl0, 'a) code) code = .<fun x_1 -> .<x_1>.>. 
*)
let true = .! ((.! t4) true);;

(* From Problems.txt  Mon Jan 10 18:51:21 GMTST 2005
  CSP constants in Pervasives (and similar) are type checked only once for
  a given occurrence.
   # let f x  = .< ref .~ x>.
     in (.! (f .<3>.), .! (f .<1.3>.));;
   This expression has type int but is here used with type float
*)

let t5 =
  let f x  = .< ref .~ x>.
  in (.! (f .<3>.), .! (f .<1.3>.));;

(*
val t5 : int ref * float ref = ({contents = 3}, {contents = 1.3})
*)

(* From Problems.txt Tue Jan 18 14:08:52 GMTST 2005
  type aliases are not handled correctly in code, example:
    # type a = int;;
    # let f (x:a) = 1;;
    # .! .<f 1>.;;
*)
type a = int;;
let 1 =
  let f (x:a) = 1 in
  .! .<f 1>.;;

(* From Problems.txt Oct 3, 2006 Printing of records in brackets *)

let t7 = 
  let open Complex in
  .<let x = {re=1.0; im=2.0} in
    let y = {x with re = 2.0} in
    y>.
 ;;

(*
val t7 : ('cl, Complex.t) code = .<
  let x_39 = {Complex.re = 1.0; Complex.im = 2.0} in
  let y_40 = {x_39 with Complex.re = 2.0} in y_40>. 
*)

let {Complex.re=2.0; im=2.0} = .! t7;;

(* First-class polymorphism *)

let tfc1 = {Runcode.cde = .<1>.};;
(* - : int Runcode.cde = .<1>. *)
let 1 = Runcode.run {Runcode.cde = .<1>.};;

let tfc2 = .<{Runcode.cde = .<1>.}>.;;
(*
- : ('cl, int Runcode.cde) code = .<{Runcode.cde = .<1>.}>. 
*)
let tfc3 = .! .<{Runcode.cde = .<1>.}>.;;
(* - : int Runcode.cde = .<1>.  *)
let tfc4 = {Runcode.cde= .<{Runcode.cde = .<1>.}>.};;
(* - : int Runcode.cde Runcode.cde = .<{Runcode.cde = .<1>.}>.  *)
let tfc5 = Runcode.run {Runcode.cde= .<{Runcode.cde = .<1>.}>.};;
(* - : int Runcode.cde = .<1>.  *)
let 1 = Runcode.run (Runcode.run {Runcode.cde= .<{Runcode.cde = .<1>.}>.});;
(* - : int = 1 *)

Printf.printf "\nAll Done\n";;
