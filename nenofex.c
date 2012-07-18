/*
 This file is part of Nenofex.

 Nenofex, an expansion-based QBF solver for negation normal form.        
 Copyright 2008, 2012 Florian Lonsing.

 Nenofex is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or (at
 your option) any later version.

 Nenofex is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Nenofex.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/resource.h>
#include "../picosat/picosat.h"
#include "nenofex_types.h"
#include "stack.h"
#include "mem.h"


#define VERSION						\
  "Nenofex 1.0\n"					\
  "Copyright 2008, 2012 Florian Lonsing.\n"	\
"This is free software; see COPYING for copying conditions.\n"		\
"There is NO WARRANTY, to the extent permitted by law.\n"


#define USAGE \
"usage: nenofex [<option> ...] [ <in-file> ]\n"\
"\n"\
"where <in-file> is a file in (Q)DIMACS format and <option> is any of the following:\n\n\n"\
"Printing Information:\n"\
"---------------------\n\n"\
"  -h | -help			print usage information\n"\
"  --version                     print version\n"\
"  -v				verbose output (default: only QDIMACS output)\n"\
"  --show-progress		print short summary after each expansion step\n"\
"  --show-graph-size		print graph size after each expansion step\n\n\n"\
"SAT Solving:\n"\
"------------\n\n"\
"  --no-sat-solving		never call internal SAT-solver even if formula is\n"\
"				  purely existential/universal\n"\
"  --verbose-sat-solving 	enable verbosity mode during SAT-solving\n"\
"  --dump-cnf			print generated CNF (if any) to 'stdout'\n"\
"	       			  (may be combined with '--no-sat-solving')\n"\
"  --cnf-generator=<cnf-gen>	set NNF-to-CNF generator where <cnf-gen> is either \n"\
"				  'tseitin' or 'tseitin_revised' (default)\n\n\n"\
"Expansion:\n"\
"----------\n\n"\
"  --full-expansion		do not stop expanding variables even if formula is\n"\
"				  purely existential/universal\n"\
"  -n <val>			expand at most <val> variables where \n"\
"				  <val> is a positive integer\n"\
"  --size-cutoff=<val>		stop expanding if graph size after an expansion\n"\
"				  step has grown faster than specified where\n" \
"				  <val> is either\n"\
"				    - a floating point value between -1.0 and 1.0\n"\
"		       		      and cutoff occurs if 'new_size > old_size * (1 + <val>)'\n"\
"				  or\n"\
"				    - an integer and cutoff occurs if 'new_size > (old_size + <val>)'\n"\
"  --cost-cutoff=<val>		stop expanding if predicted minimal expansion\n"\
"				  costs exceed <val> where val is an integer\n\n\n"\
"  --univ-trigger=<val>	       	enable non-inner universal expansion if tree has grown\n"\
"				  faster than <val> (default: 10) nodes in last exist. expansion\n"\
"  --univ-delta=<val>	       	increase universal trigger by <val> after \n"\
"				  universal expansion (default: 10)\n"\
"  --post-expansion-flattening	affects the following situation only: \n"\
"				  existential variable 'x' has AND-LCA and either\n"\
"				  has exactly one positive occurrence and <n> negative \n"\
"				  ones or vice versa, or variable 'x' has exactly two\n"\
"				  positive and two negative occurrences -> flatten subgraph\n"\
"				  rooted at 'split-OR' by multiplying out clauses\n\n\n" \
"Optimizations:\n"\
"--------------\n\n"\
"  --show-opt-info		print short info after calls of optimizations\n"\
"  --opt-subgraph-limit=<val>	impose size limit <val> (default: 500) on\n"\
"				  subgraph where optimizations are carried out\n"\
"  --no-optimizations		do not optimize by global flow and redundancy removal\n"\
"  --no-atpg			do not optimize by ATPG redundancy removal\n"\
"				  (overruled by '--no-optimizations')\n"\
"  --no-global-flow		do not optimize by global flow\n"\
"				  (overruled by '--no-optimizations')\n"\
"  --propagation-limit=<val>	set hard propagation limit in optimizations (see below)"\
"\n\n\n"\
"REMARKS:\n\n"\
"  - For calling the solver on a CNF, you should specify '--full-expansion'\n\n"\
"  - If '-n <val>' is specified the solver will - if possible - forward a CNF\n"\
"      to the internal SAT solver unless '--no-sat-solving' is specified\n\n"\
"  - Options '--size-cutoff=<val>', '--cost-cutoff=<val>' and '-n <val>' may be combined\n\n"\
"  - Option '--propagation-limit=<val>' will set a limit for global flow optimization\n"\
"      and redundancy removal separately, i.e. both optimizations may perform <val>\n"\
"      propagations. If this option is omitted (default) then a built-in limit will\n"\
"      be set depending on the size of the formula subject to optimization\n\n"


/*
- wrapper-macros for calling internal SAT-solver
*/
#define sat_solver_init() (picosat_init())
#define sat_solver_verbosity_mode() (picosat_enable_verbosity())
#define sat_solver_reset() (picosat_reset())
#define sat_solver_add(lit) (picosat_add((lit)))
#define sat_solver_sat() (picosat_sat(-1))
#define sat_solver_deref(lit) (picosat_deref(lit))
#define SAT_SOLVER_RESULT_UNKNOWN PICOSAT_UNKNOWN
#define SAT_SOLVER_RESULT_SATISFIABLE PICOSAT_SATISFIABLE
#define SAT_SOLVER_RESULT_UNSATISFIABLE PICOSAT_UNSATISFIABLE


/*
- full-scale assertion checking
- involves full traversals of whole graph, occurrence lists,...
- VERY expensive!
- flag 'NDEBUG' overules any setting of the following
*/
#if 1

#define ASSERT_AFTER_EXP_ALL_NON_INNERMOST_SCOPE_VARS_UNINIT 0
#define ASSERT_BEFORE_FIND_VAR_CCA_FULL_GRAPH_INTEGRITY 0
#define ASSERT_AFTER_FIND_VAR_LCA_FULL_GRAPH_INTEGRITY 0

#define ASSERT_LCA_OBJECT_INTEGRITY 0
#define ASSERT_ALL_LCA_CHILDREN_UNMARKED 0

#define ASSERT_COPY_EQUALS 0

#define ASSERT_SIZES_IN_EXPANSION 0
#define PRINT_SIZE_CHECK_INFO 0

#define ASSERT_GRAPH_AFTER_EXP_BEFORE_PROP 0
#define ASSERT_GRAPH_AFTER_EXP_AFTER_PROP 0

#define ASSERT_GRAPH_AFTER_ATPG_GLOBAL_FLOW 0

#define ASSERT_GRAPH_BETWEEN_DEC_SCORE 0
#define ASSERT_GRAPH_AFTER_DEC_SCORE 0
#define ASSERT_INIT_SCOPE_SCORES_ALL_UNMARKED 0
#define ASSERT_INIT_VAR_SCORES_ALL_UNMARKED 0
#define ASSERT_GRAPH_AFTER_PARSING 0
#define ASSERT_SIMP_ONE_LEVEL_BEFORE 0

#define ASSERT_POS_IN_CHANGED_CH_LIST 0

#define ASSERT_PRIORITY_QUEUE_HEAP_CONDITION 0
#define ASSERT_ALL_SCOPES_FREE_OF_NO_OCC_VARS 0
#define ASSERT_ALL_SCOPES_PRIORITY_QUEUE_HEAP_CONDITION 0

#define ASSERT_UNIVERSAL_LCA_COMPUTATION_GRAPH_AND_VARS 0
#define ASSERT_PREPARE_NON_INNERMOST_UNIVERSAL_EXPANSION 0
#define ASSERT_ALL_DEPENDING_VARS_CLEANED_UP 0
#define DELETE_ELEM_ASSERT_PRIORITY_QUEUE_HEAP_CONDITION 0

#define ASSERT_SCOPE_VAR_CNT 0

#define ASSERT_POST_EXPANSION_FLATTENING 0

#define ASSERT_ALL_LCA_OCC_POS_INTEGRITY 0

#else

#define ASSERT_AFTER_EXP_ALL_NON_INNERMOST_SCOPE_VARS_UNINIT 1
#define ASSERT_BEFORE_FIND_VAR_LCA_FULL_GRAPH_INTEGRITY 1
#define ASSERT_AFTER_FIND_VAR_LCA_FULL_GRAPH_INTEGRITY 1
#define ASSERT_LCA_OBJECT_INTEGRITY 1
#define ASSERT_ALL_LCA_CHILDREN_UNMARKED 1
#define ASSERT_COPY_EQUALS 1
#define ASSERT_SIZES_IN_EXPANSION 1
#define PRINT_SIZE_CHECK_INFO 0
#define ASSERT_GRAPH_AFTER_EXP_BEFORE_PROP 1
#define ASSERT_GRAPH_AFTER_EXP_AFTER_PROP 1

#define ASSERT_GRAPH_AFTER_ATPG_GLOBAL_FLOW 1

#define ASSERT_GRAPH_BETWEEN_DEC_SCORE 1
#define ASSERT_GRAPH_AFTER_DEC_SCORE 1
#define ASSERT_INIT_SCOPE_SCORES_ALL_UNMARKED 1
#define ASSERT_INIT_VAR_SCORES_ALL_UNMARKED 1
#define ASSERT_GRAPH_AFTER_PARSING 1
#define ASSERT_SIMP_ONE_LEVEL_BEFORE 1

#define ASSERT_POS_IN_CHANGED_CH_LIST 1

#define ASSERT_PRIORITY_QUEUE_HEAP_CONDITION 1
#define ASSERT_ALL_SCOPES_FREE_OF_NO_OCC_VARS 1
#define ASSERT_ALL_SCOPES_PRIORITY_QUEUE_HEAP_CONDITION 1

#define ASSERT_UNIVERSAL_LCA_COMPUTATION_GRAPH_AND_VARS 1
#define ASSERT_PREPARE_NON_INNERMOST_UNIVERSAL_EXPANSION 1
#define ASSERT_ALL_DEPENDING_VARS_CLEANED_UP 1
#define DELETE_ELEM_ASSERT_PRIORITY_QUEUE_HEAP_CONDITION 1

#define ASSERT_SCOPE_VAR_CNT 1

#define ASSERT_POST_EXPANSION_FLATTENING 1

#define ASSERT_ALL_LCA_OCC_POS_INTEGRITY 1

#endif /* end: full-scale assertion checking */


/*
- print info about number of updated and remaining vars after each expansion
*/
#define PRINT_INCREMENTAL_COST_UPDATE 0


/*
- compute statistics after each expansion and print to 'stderr'
- rather expensive (full traversal of graph)
*/
#define COMPUTE_GRAPH_STATISTICS 0


/*
- print number of clauses that would be generated by revised tseitin transf.
- rather expensive!
- FOR CNFs ONLY: WILL RAISE ASSERTION FAILURE IF CALLED ON QBFs!
*/
#define PRINT_CLAUSE_COUNT 0


#define PRINT_LIT_STATS_BEFORE_EXP 0
#define ASSERT_PRINT_LIT_STATS_BEFORE_EXP 0


/* optional statistics */
#if 0
#define COMPUTE_NUM_DELETED_NODES 1
#define COMPUTE_NUM_NON_INC_EXPANSIONS_IN_SCORES 1
#define COMPUTE_NUM_NON_INC_EXPANSIONS 1
#define COMPUTE_CASES_IN_EXPANSIONS 1
#define COMPUTE_AVERAGE_UPDATE_MARKED_RATIOS 1
#define COMPUTE_NUM_UNITS 1
#define COMPUTE_NUM_UNATES 1
#define COMPUTE_NUM_TOTAL_CREATED_NODES 1
#define COMPUTE_MAX_TREE_SIZE 1
#define COMPUTE_LCA_PARENT_VISITS 1
#endif
/* END: OPTIONAL STATISTICS */


/* 
- minor fix for opt-subgraph size reduction: ensure max. size of subgraph 
- enabling recommended, since this behaviour is wanted
*/
#define MAXIMIZE_REDUCED 1


/* delete pos-lists in nodes when empty */
#define DELETE_EMPTY_STACKS_IN_NODES 1


/* pick non-innermost universal variable with fewest literals */
#define APPROXIMATE_UNIV_SCORES 0


#define DEFAULT_STACK_SIZE 128
#define DEFAULT_QUEUE_SIZE 128
#define DEFAULT_SCOPE_NESTING INT_MAX


#define LCA_CHILDREN_INIT_SIZE 16


/*
- marking
*/
#define SIMPLIFY_VAR_POS_MARK 1
#define SIMPLIFY_VAR_NEG_MARK 2
#define simplify_var_marked(var) ((var)->simp_mark)
#define simplify_var_unmark(var) ((var)->simp_mark = 0)
#define simplify_var_pos_marked(var) ((var)->simp_mark == SIMPLIFY_VAR_POS_MARK)
#define simplify_var_neg_marked(var) ((var)->simp_mark == SIMPLIFY_VAR_NEG_MARK)
#define simplify_var_pos_mark(var) ((var)->simp_mark = SIMPLIFY_VAR_POS_MARK)
#define simplify_var_neg_mark(var) ((var)->simp_mark = SIMPLIFY_VAR_NEG_MARK)

/* #ifndef NDEBUG */
#define size_subformula_marked(node) ((node)->mark3)
#define size_subformula_mark(node) ((node)->mark3 = 1)
#define size_subformula_unmark(node) ((node)->mark3 = 0)
/* #endif */

#define lca_child_marked(node) ((node)->mark2)
#define mark_lca_child(node) ((node)->mark2 =  1)
#define unmark_lca_child(node) ((node)->mark2 = 0)

#define universal_lca_child_marked(node) ((node)->mark3)
#define mark_universal_lca_child(node) ((node)->mark3 = 1)
#define unmark_universal_lca_child(node) ((node)->mark3 = 0)

#define dependency_visit_marked(node) ((node)->mark1)
#define mark_dependency_visit(node) ((node)->mark1 = 1)
#define unmark_dependency_visit(node) ((node)->mark1 = 0)

/*
- operator nodes are marked during copying
*/
#define copy_formula_marked(node) ((node)->mark2)
#define copy_formula_mark(node) ((node)->mark2 = 1)
#define copy_formula_unmark(node) ((node)->mark2 = 0)

/*
- literals are marked during expansion
*/
#define truth_propagation_marked(node) ((node)->mark2)
#define truth_propagation_mark(node) ((node)->mark2 = 1)
#define truth_propagation_unmark(node) ((node)->mark2 = 0)

#define cost_update_marked(var) \
(((var)->lca_update_mark) || ((var)->inc_score_update_mark) \
   || ((var)->dec_score_update_mark))

#define decrease_score_marked(node) ((node)->mark1)
#define decrease_score_mark(node) ((node)->mark1 = 1)
#define decrease_score_unmark(node) ((node)->mark1 = 0)

#define decrease_score_collected_marked(node) ((node)->mark2)
#define decrease_score_collected_mark(node) ((node)->mark2 = 1)
#define decrease_score_collected_unmark(node) ((node)->mark2 = 0)


/* END: MACROS, CONFIGURATION */


/* ---------- START: PRIORITY QUEUE ---------- */

static void
create_priority_queue (Stack ** priority_heap, unsigned int init_capacity)
{
  *priority_heap = create_stack (init_capacity);
}


static void
delete_priority_queue (Stack ** priority_heap)
{
  delete_stack (*priority_heap);
  *priority_heap = 0;
}


static void
add_fast_priority_queue (Stack * priority_heap, Var * var)
{
  assert (var->priority_pos == -1);

  push_stack (priority_heap, var);
  var->priority_pos = count_stack (priority_heap) - 1;

  assert (var->priority_pos >= 0);
  assert ((unsigned int) var->priority_pos < count_stack (priority_heap));
}


static int
get_left_child_pos (int cur_pos, int size)
{
  assert (cur_pos >= 0);
  assert (cur_pos < size);

  int result;

  result = 2 * cur_pos + 1;

  return result;
}


static int
get_right_child_pos (int cur_pos, int size)
{
  assert (cur_pos >= 0);
  assert (cur_pos < size);

  int result;

  result = 2 * (cur_pos + 1);

  return result;
}


static int
get_parent_pos (int cur_pos, int size)
{
  assert (cur_pos > 0);
  assert (cur_pos < size);

  int result;

  result = (cur_pos - 1) / 2;

  assert (result < size);

  assert (cur_pos == get_right_child_pos (result, size) ||
          cur_pos == get_left_child_pos (result, size));

  return result;
}


#ifndef NDEBUG
static void
assert_priority_queue_heap_condition (Stack * priority_heap)
{
  int size = count_stack (priority_heap);

  void **start = priority_heap->elems;

  int pos, no_children, left_child_pos, right_child_pos;
  no_children = size / 2;

  for (pos = 0; pos < size; pos++)
    {
      Var *cur_var, *left_var, *right_var;

      cur_var = start[pos];
      assert (cur_var->priority_pos == pos);

      left_child_pos = get_left_child_pos (pos, size);
      right_child_pos = get_right_child_pos (pos, size);

      if (pos < no_children)
        {
          assert (left_child_pos < size);

          left_var = start[left_child_pos];

          if (right_child_pos < size)
            right_var = start[right_child_pos];

          assert (cur_var->exp_costs.score <= left_var->exp_costs.score);
          assert (right_child_pos >= size ||
                  cur_var->exp_costs.score <= right_var->exp_costs.score);
        }
      else                      /* has no children */
        {
          assert (right_child_pos >= size);
          assert (left_child_pos >= size);
        }
    }                           /* end: for */

}
#endif


static int
compare_priority_queue (Stack * priority_heap, int pos_a, int pos_b)
{
  assert (pos_a >= 0);
  assert ((unsigned int) pos_a < count_stack (priority_heap));
  assert (pos_b >= 0);
  assert ((unsigned int) pos_b < count_stack (priority_heap));

  int result;

  void **start = priority_heap->elems;

  Var *var_a = start[pos_a];
  Var *var_b = start[pos_b];

  int var_a_score = var_a->exp_costs.score;
  int var_b_score = var_b->exp_costs.score;

  if (var_a_score < var_b_score)
    result = -1;
  else if (var_a_score == var_b_score)
    result = 0;
  else
    result = 1;

  return result;
}


static void
swap_priority_queue (Stack * priority_heap, int pos_a, int pos_b)
{
  assert (pos_a != pos_b);
  assert ((unsigned int) pos_a < count_stack (priority_heap));
  assert ((unsigned int) pos_b < count_stack (priority_heap));

  void **a, **b, **start;

  start = priority_heap->elems;

  a = start + pos_a;
  b = start + pos_b;

  Var *var_a, *var_b;

  var_a = *a;
  var_b = *b;

  assert (var_a->priority_pos == pos_a);
  assert (var_b->priority_pos == pos_b);

  *a = var_b;
  var_b->priority_pos = pos_a;

  *b = var_a;
  var_a->priority_pos = pos_b;
}


static void
up_heap (Stack * priority_heap, int cur_pos)
{
  int size = count_stack (priority_heap);

  assert (cur_pos < size);

  while (cur_pos > 0)
    {
      int parent_pos = get_parent_pos (cur_pos, size);

      if (compare_priority_queue (priority_heap, cur_pos, parent_pos) >= 0)
        break;

      swap_priority_queue (priority_heap, cur_pos, parent_pos);
      cur_pos = parent_pos;
    }                           /* end: while top not reached */
}


static void
down_heap (Stack * priority_heap, int cur_pos)
{
  int child_pos, left_child_pos, right_child_pos;
  int size = count_stack (priority_heap);

  assert (cur_pos >= 0);
  assert (cur_pos < size);

  for (;;)
    {
      left_child_pos = get_left_child_pos (cur_pos, size);

      if (left_child_pos >= size)
        break;                  /* has no left child */

      right_child_pos = get_right_child_pos (cur_pos, size);

      if (right_child_pos < size &&
          compare_priority_queue (priority_heap, left_child_pos,
                                  right_child_pos) > 0)
        child_pos = right_child_pos;
      else
        child_pos = left_child_pos;

      if (compare_priority_queue (priority_heap, cur_pos, child_pos) > 0)
        {
          swap_priority_queue (priority_heap, cur_pos, child_pos);
          cur_pos = child_pos;
        }
      else
        break;
    }                           /* end: for */
}


static void
init_order_priority_queue (Stack * priority_heap)
{
  int cur_pos, end_pos = count_stack (priority_heap);

  for (cur_pos = 0; cur_pos < end_pos; cur_pos++)
    {
      up_heap (priority_heap, cur_pos);
    }

#ifndef NDEBUG
#if ASSERT_PRIORITY_QUEUE_HEAP_CONDITION
  assert_priority_queue_heap_condition (priority_heap);
#endif
#endif
}


static void
delete_elem_priority_queue (Stack * priority_heap, int elem_pos)
{
  assert (elem_pos >= 0);
  assert ((unsigned int) elem_pos < count_stack (priority_heap));

#ifndef NDEBUG
#if DELETE_ELEM_ASSERT_PRIORITY_QUEUE_HEAP_CONDITION
  assert_priority_queue_heap_condition (priority_heap);
#endif
#endif

  Var *last, *del;
  void **pos = priority_heap->elems + elem_pos;

  del = *pos;
  assert (del->priority_pos == elem_pos);
  del->priority_pos = -1;

  last = pop_stack (priority_heap);

  if (del != last)
    {
      *pos = last;
      last->priority_pos = elem_pos;
      up_heap (priority_heap, elem_pos);
      down_heap (priority_heap, elem_pos);
    }

#ifndef NDEBUG
#if DELETE_ELEM_ASSERT_PRIORITY_QUEUE_HEAP_CONDITION
  assert_priority_queue_heap_condition (priority_heap);
#endif
#endif
}


static Var *
remove_min (Stack * priority_heap)
{
  void **start = priority_heap->elems;

  if (start == priority_heap->top)
    return 0;

  Var *min_var, *last_var;

  min_var = *start;
  min_var->priority_pos = -1;

  last_var = pop_stack (priority_heap);

  assert (last_var != min_var || count_stack (priority_heap) == 0);

  if (min_var == last_var)
    return min_var;

  *start = last_var;
  last_var->priority_pos = 0;

  if (count_stack (priority_heap) >= 2)
    {
      down_heap (priority_heap, 0);
    }

#ifndef NDEBUG
#if ASSERT_PRIORITY_QUEUE_HEAP_CONDITION
  assert_priority_queue_heap_condition (priority_heap);
#endif
#endif

  return min_var;
}


static Var *
access_min (Stack * priority_heap)
{
  void **start = priority_heap->elems;

  if (start == priority_heap->top)
    return 0;
  else
    return *start;
}


static void
decrease_key (Stack * priority_heap, Var * var)
{
  int cur_pos = var->priority_pos;

  assert (cur_pos >= 0);
  assert ((unsigned int) cur_pos < count_stack (priority_heap));
  assert (var == priority_heap->elems[cur_pos]);

  up_heap (priority_heap, cur_pos);
}


static void
increase_key (Stack * priority_heap, Var * var)
{
  int cur_pos = var->priority_pos;

  assert (cur_pos >= 0);
  assert ((unsigned int) cur_pos < count_stack (priority_heap));
  assert (var == priority_heap->elems[cur_pos]);

  down_heap (priority_heap, cur_pos);
}


static int
update_key (Stack * priority_heap, Var * var, int old_score)
{
  int result;

  int score = var->exp_costs.score;

  if (score == old_score)
    result = 0;
  else if (score < old_score)
    {
      decrease_key (priority_heap, var);
      result = -1;
    }
  else
    {
      increase_key (priority_heap, var);
      result = 1;
    }

#ifndef NDEBUG
#if ASSERT_PRIORITY_QUEUE_HEAP_CONDITION
  if (result)
    assert_priority_queue_heap_condition (priority_heap);
#endif
#endif

  return result;
}

/* ---------- END: PRIORITY QUEUE ---------- */

static double
time_stamp ()
{
  double result = 0;
  struct rusage usage;

  if (!getrusage (RUSAGE_SELF, &usage))
    {
      result += usage.ru_utime.tv_sec + 1e-6 * usage.ru_utime.tv_usec;
      result += usage.ru_stime.tv_sec + 1e-6 * usage.ru_stime.tv_usec;
    }

  return result;
}


/*
- costs of vars on stack 'vars_marked_for_update' will be updated from scratch
- NEW: collect variables from cur. scope only 
*/
void
collect_variable_for_update (Nenofex * nenofex, Var * var)
{
  if (!nenofex->cur_scope)
    return;

  assert (cost_update_marked (var));

  if (!var->collected_for_update && var->scope == *nenofex->cur_scope)
    {
      var->collected_for_update = 1;
      push_stack (nenofex->vars_marked_for_update, var);
    }
}


static void init_lca_object (LCAObject * lca_object);


static Nenofex *
create_nenofex ()
{
  size_t num_bytes = sizeof (Nenofex);
  Nenofex *result = mem_malloc (num_bytes);
  assert (result);
  memset (result, 0, num_bytes);

  result->scopes = create_stack (DEFAULT_STACK_SIZE);
  result->unates = create_stack (DEFAULT_STACK_SIZE);
  result->depending_vars = create_stack (DEFAULT_STACK_SIZE);
  result->vars_marked_for_update = create_stack (DEFAULT_STACK_SIZE);
  result->atpg_rr = create_atpg_redundancy_remover ();

  init_lca_object (&(result->changed_subformula));

  return result;
}


static void delete_node (Nenofex * nenofex, Node * node);


/*
- delete whole graph
*/
static void
free_graph (Nenofex * nenofex)
{
  assert (nenofex);

  if (!nenofex->graph_root)
    return;

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (!is_literal_node (cur))
        {
          Node *ch;
          for (ch = cur->child_list.last; ch; ch = ch->level_link.prev)
            {
              push_stack (stack, ch);
            }
        }

      if (cur->lca_child_list_occs)
        {
          delete_stack (cur->lca_child_list_occs);
          assert (cur->pos_in_lca_children);
          delete_stack (cur->pos_in_lca_children);
        }

      mem_free (cur, sizeof (Node));
    }                           /* end: while stack not empty */

  delete_stack (stack);
}


static void free_lca_children (LCAObject * lca_object);


static void delete_scope (Scope * scope);


static void
free_nenofex (Nenofex * nenofex)
{
  assert (!nenofex->vars);

  free_graph (nenofex);

  free_lca_children (&(nenofex->changed_subformula));

  Scope *scope;
  while ((scope = pop_stack (nenofex->scopes)))
    {
      Var *var;
      while ((var = pop_stack (scope->vars)))
        {
          delete_stack (var->pos_in_lca_child_list_occs);
          free_lca_children (&(var->exp_costs.lca_object));
          assert (!var->subformula_pos_occs);
          assert (!var->subformula_neg_occs);
          mem_free (var, sizeof (Var));
        }
      delete_scope (scope);
    }
  delete_stack (nenofex->scopes);

  delete_stack (nenofex->unates);
  delete_stack (nenofex->vars_marked_for_update);
  delete_stack (nenofex->depending_vars);

  free_atpg_redundancy_remover (nenofex->atpg_rr);

  mem_free (nenofex, sizeof (Nenofex));
}


/*
- create new AND-operator node
- node ID actually not relevant
*/
Node *
and_node (Nenofex * nenofex)
{
  assert (nenofex->next_free_node_id > nenofex->num_orig_vars);

#if COMPUTE_NUM_TOTAL_CREATED_NODES
  nenofex->stats.num_total_created_nodes++;
#endif

  size_t num_bytes = sizeof (Node);
  Node *result = mem_malloc (num_bytes);
  assert (result);
  memset (result, 0, num_bytes);

  result->type = NODE_TYPE_AND;
  result->id = nenofex->next_free_node_id++;

  return result;
}


/*
- create new OR-operator node
- node ID actually not relevant
*/
Node *
or_node (Nenofex * nenofex)
{
  assert (nenofex->next_free_node_id > nenofex->num_orig_vars);

#if COMPUTE_NUM_TOTAL_CREATED_NODES
  nenofex->stats.num_total_created_nodes++;
#endif

  size_t num_bytes = sizeof (Node);
  Node *result = mem_malloc (num_bytes);
  assert (result);
  memset (result, 0, num_bytes);

  result->type = NODE_TYPE_OR;
  result->id = nenofex->next_free_node_id++;

  return result;
}


/*
- create new literal node
- assign node ID actually with respect to variable and negation
*/
static Node *
lit_node (Nenofex * nenofex, int lit, Var * var)
{
  assert (lit);

#if COMPUTE_NUM_TOTAL_CREATED_NODES
  nenofex->stats.num_total_created_nodes++;
#endif

  size_t num_bytes = sizeof (Node);
  Node *result = (Node *) mem_malloc (num_bytes);
  assert (result);
  memset (result, 0, num_bytes);

  result->id = lit;
  result->type = NODE_TYPE_LITERAL;
  assert (var->id);
  assert (var->lits[0].var);
  assert (var->lits[1].var);
  result->lit = (lit < 0 ? &(var->lits[0]) : &(var->lits[1]));

  return result;
}


/*
- collect possible unates incrementally whenever one occ. list becomes empty
*/
static void
collect_variable_as_unate (Nenofex * nenofex, Var * var)
{
  if (!var->collected_as_unate)
    {
      var->collected_as_unate = 1;
      push_stack (nenofex->unates, var);
    }
}


/*
- set up new variable
- TO BE CHANGED: variables are collected twice (in scope and on separate stack)
- push all new vars on unate-stack; afterwards unates are detected incrementally
*/
static void
init_variable (Nenofex * nenofex, unsigned int abs_lit, Scope * scope)
{
  assert (abs_lit);
  assert (!(nenofex->vars[abs_lit]));

  Var *var = (Var *) mem_malloc (sizeof (Var));
  assert (var);
  memset (var, 0, sizeof (Var));

  var->exp_costs.score = INT_MIN;

  var->pos_in_lca_child_list_occs = create_stack (DEFAULT_STACK_SIZE);

  init_lca_object (&(var->exp_costs.lca_object));

  nenofex->vars[abs_lit] = var;

  var->id = abs_lit;
  var->priority_pos = -1;
  var->lits[0].var = var;
  var->lits[0].negated = 1;
  var->lits[1].var = var;
  var->lits[1].negated = 0;
  var->lits[0].occ_list.first = (Node *) 0;
  var->lits[0].occ_list.last = (Node *) 0;
  var->lits[1].occ_list.first = (Node *) 0;
  var->lits[1].occ_list.last = (Node *) 0;

  if (!scope)                   /* add variable to default scope */
    {
      scope = nenofex->scopes->elems[0];
      assert (scope->nesting == DEFAULT_SCOPE_NESTING);
    }
  push_stack (scope->vars, var);
  scope->remaining_var_cnt++;

  /* NEW */
  add_fast_priority_queue (scope->priority_heap, var);

  var->scope = scope;

  collect_variable_as_unate (nenofex, var);

  assert (!(nenofex->vars[abs_lit]->lits[0].occ_list.first));
  assert (!(nenofex->vars[abs_lit]->lits[1].occ_list.first));
  assert (!(nenofex->vars[abs_lit]->lits[0].occ_list.last));
  assert (!(nenofex->vars[abs_lit]->lits[1].occ_list.last));
}


#ifndef NDEBUG

/*
- for assertion checking only
*/
static void
assert_occ_list_integrity (Nenofex * nenofex, Lit * lit)
{
  Node *node, *prev;
  prev = 0;
  for (node = lit->occ_list.first; node; node = node->occ_link.next)
    {
      assert (node->lit == lit);
      assert (node->num_children == 0);
      assert (node->size_subformula == 1);
      assert (is_literal_node (node));

      assert (!node->child_list.first);
      assert (!node->child_list.last);

      assert (nenofex->cur_expanded_var || !node->mark2);
      assert (nenofex->cur_expanded_var || !node->mark2);

      assert (node->id);
      assert (node->id == (lit->negated ? -lit->var->id : lit->var->id));
      assert (!prev || node->occ_link.prev == prev);
      assert (!prev || prev->occ_link.next == node);
      assert (node != lit->occ_list.first || !node->occ_link.prev);
      assert (node != lit->occ_list.last || !node->occ_link.next);
      prev = node;
    }
}


/*
- for assertion checking only
*/
static void
assert_all_occ_lists_integrity (Nenofex * nenofex)
{
  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;

  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;

      void **v_var, **var_end;
      var_end = scope->vars->top;

      for (v_var = scope->vars->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;

          assert (var->id);
          assert (var->scope);
          assert (!var->simp_mark);

          assert (nenofex->atpg_rr_called || !var->atpg_mark);
          assert (nenofex->atpg_rr_called || !var->assignment);

          assert_occ_list_integrity (nenofex, &(var->lits[0]));
          assert_occ_list_integrity (nenofex, &(var->lits[1]));
        }                       /* end: for all scope vars */

    }                           /* end: for all scopes */
}


#ifndef NDEBUG
static unsigned int count_children (Nenofex * nenofex, Node * node);
#endif


/*
- for assertion checking only
*/
static void
assert_child_occ_list_integrity (Nenofex * nenofex, Node * parent)
{
  assert (parent->num_children == count_children (nenofex, parent));

  assert (parent != nenofex->graph_root || !parent->parent);
  assert (parent->parent || parent == nenofex->graph_root);

  /* atpg_info-pointers expected to be cleared properly */
  assert (nenofex->cur_expanded_var || !parent->atpg_info);
  assert (nenofex->cur_expanded_var || !parent->mark1);

  assert (!parent->mark2);

  if (is_literal_node (parent))
    {
      assert (!parent->child_list.first);
      assert (!parent->child_list.last);
      assert (parent->lit);
      assert (!parent->lit->var->simp_mark);
      assert (parent->id);
      assert (parent->lit->negated || parent->id > 0);
      assert (!parent->lit->negated || parent->id < 0);
      assert (parent->id == (parent->lit->negated ?
                             -parent->lit->var->id : parent->lit->var->id));

      assert (parent->lit->occ_list.first);
      assert (parent->lit->occ_list.last);
      assert (parent->occ_link.next || parent->lit->occ_list.last == parent);
      assert (parent->occ_link.prev || parent->lit->occ_list.first == parent);
    }
  else
    {
      assert (parent->id > 0);
      assert (!parent->lit);
      assert (!parent->occ_link.next);
      assert (!parent->occ_link.prev);
      assert ((unsigned int) parent->id > nenofex->num_orig_vars);
      assert (is_or_node (parent) || is_and_node (parent));

      if (!parent->child_list.first)
        {
          assert (!parent->child_list.last);
          assert (0);
        }
      else
        {
          assert (parent->child_list.last);
          assert (parent->child_list.first != parent->child_list.last);

          int op_ch_found = 0;
          Node *ch, *prev;
          prev = 0;
          for (ch = parent->child_list.first; ch; ch = ch->level_link.next)
            {
              op_ch_found = (op_ch_found || !is_literal_node (ch));

              assert (!op_ch_found || !is_literal_node (ch));

              /* atpg_info->pointers expected to be cleared properly */
              assert (nenofex->cur_expanded_var || !ch->atpg_info);
              assert (nenofex->cur_expanded_var || !ch->mark1);

              assert (!prev || ch->level_link.prev == prev);
              assert (!prev || prev->level_link.next == ch);
              assert (ch->parent->type != ch->type);
              assert (ch->parent == parent);
              assert (ch->level == parent->level + 1);
              assert (ch != parent->child_list.first || !ch->level_link.prev);
              assert (ch != parent->child_list.last || !ch->level_link.next);
              prev = ch;
            }
        }
    }                           /* end: op node */
}


/*
- for assertion checking only
*/
static void
assert_all_child_occ_lists_integrity (Nenofex * nenofex)
{
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert_child_occ_list_integrity (nenofex, cur);
      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            {
              push_stack (stack, child);
            }
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);
}


/*
- for assertion checking only
*/
static Node *
find_node_by_id (Node * root, int id)
{
  Stack *stack = create_stack (16);
  Node *cur = 0;
  push_stack (stack, (void *) root);
  while ((cur = (Node *) pop_stack (stack)))
    {
      if (cur->id == id)
        break;
      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, (void *) child);
        }
    }                           /* end: while */
  delete_stack (stack);
  return cur;
}


/*
- for assertion checking only
*/
static Node *
find_node_by_address (Node * root, Node * node)
{
  Stack *stack = create_stack (16);
  Node *cur = 0;
  push_stack (stack, (void *) root);
  while ((cur = (Node *) pop_stack (stack)))
    {
      if (cur == node)
        break;
      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, (void *) child);
        }
    }                           /* end: while */
  delete_stack (stack);
  return cur;
}

#endif /* end: ifndef NDEBUG */


void remove_and_free_subformula (Nenofex * nenofex, Node * root);


/* 
- simplification by inspection of child-list of a node
- will remove double literals or delete node if has two complementary literals
- called whenever a node 'root' gets a new literal-child
- INVARIANT: lits are stored first in child-list -> abort search at first op-child
- TODO: need no stack for marked nodes any more when lits are stored first in ch-list
*/
void
simplify_one_level (Nenofex * nenofex, Node * root)
{
  assert (!is_literal_node (root));

  if (root == nenofex->graph_root && is_and_node (root))
    {
      /* in this case need not check literal-children at graph root (AND) 
         except within redundancy removal and global flow */
      if (!nenofex->atpg_rr_called)
        return;
    }

#ifndef NDEBUG
#if ASSERT_SIMP_ONE_LEVEL_BEFORE
  assert_all_occ_lists_integrity (nenofex);
#endif
#endif

  Node *del_node = 0;
  Stack *marked = create_stack (DEFAULT_STACK_SIZE);

  Node *ch, *next;
  for (ch = root->child_list.first; ch && (is_literal_node (ch)); ch = next)
    {
      next = ch->level_link.next;

      Lit *lit = ch->lit;
      assert (lit);
      Var *var = lit->var;
      assert (var);

      /* BUG-FIX: must NOT simplify lits from var which is currently being expanded */
      if (var == nenofex->cur_expanded_var)
        continue;

      if (!simplify_var_marked (var))
        {
          if (lit->negated)
            simplify_var_neg_mark (var);
          else
            simplify_var_pos_mark (var);

          push_stack (marked, var);
        }
      else if (simplify_var_pos_marked (var))
        {
          if (lit->negated)     /* remove parent */
            {
              del_node = root;
              break;
            }
          else                  /* remove literal */
            {
              if (root->num_children == 2)      /* 'root' has to be merged after deletion */
                {
                  del_node = ch;
                  break;
                }
              else              /* can remove immediately */
                {
                  remove_and_free_subformula (nenofex, ch);
                }
            }
        }
      else
        {
          assert (simplify_var_neg_marked (var));

          if (lit->negated)     /* remove literal */
            {
              if (root->num_children == 2)      /* 'root' has to be merged after deletion */
                {
                  del_node = ch;
                  break;
                }
              else              /* can remove immediately */
                {
                  remove_and_free_subformula (nenofex, ch);
                }
            }
          else                  /* remove parent */
            {
              del_node = root;
              break;
            }
        }
    }                           /* end: for all children */

  /* unmark variables */
  Var **var, **start;
  start = (Var **) marked->elems;
  for (var = (Var **) marked->top - 1; var >= start; var--)
    {
      assert (simplify_var_marked (*var));
      simplify_var_unmark (*var);
    }

  if (del_node)
    {
      if (del_node == nenofex->graph_root)
        {
          if (is_and_node (del_node))
            nenofex->result = NENOFEX_RESULT_UNSAT;
          else
            nenofex->result = NENOFEX_RESULT_SAT;
        }
      remove_and_free_subformula (nenofex, del_node);
    }

#ifndef NDEBUG
#if ASSERT_SIMP_ONE_LEVEL_AFTER
  assert_all_occ_lists_integrity (nenofex);
#endif
#endif

  delete_stack (marked);
}


/* 
- prints list of nodes and their children as encountered in DFS
- mainly for debugging purposes
*/
static void
print_graph_by_traversal (Node * root)
{
  Stack *stack = create_stack (1);
  push_stack (stack, root);

  Node *cur;
  while ((cur = (Node *) pop_stack (stack)))
    {
      if (!is_literal_node (cur))
        {
          printf ("%d (%s): ", cur->id, is_or_node (cur) ? "||" : "&&");

          unsigned int old_cnt = count_stack (stack);

          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            {
              push_stack (stack, (void *) child);
              assert (*(stack->top - 1) == child);
            }

          void **print_ch;
          void **old_top_p = stack->elems + old_cnt;

          assert (old_top_p < stack->top - 1);

          for (print_ch = stack->top /*_p*/  - 1; print_ch >= old_top_p;
               print_ch--)
            {
              printf ("%d", ((Node *) * print_ch)->id);
              if (is_literal_node (((Node *) * print_ch)))
                printf ("L");
              if (((Node *) * print_ch)->level_link.next)
                printf (", ");
            }
          printf ("\n");
        }
    }                           /* end: while */

  delete_stack (stack);
}


/* 
- unlink a literal from occurrence list
- same code as for unlinking from child-list, but now working on occ_links
*/
static void
unlink_node_from_occ_list (Nenofex * nenofex, Node * node)
{
  assert (node);
  assert (node->lit);
  assert (is_literal_node (node));

  if (node->occ_link.prev)
    {
      node->occ_link.prev->occ_link.next = node->occ_link.next;

      if (node->occ_link.next)
        node->occ_link.next->occ_link.prev = node->occ_link.prev;
      else
        {                       /* node == last */
          assert (node == node->lit->occ_list.last);

          node->lit->occ_list.last = node->occ_link.prev;

          assert (!node->lit->occ_list.last->occ_link.next);
        }
    }
  else
    {                           /* node == first */
      assert (node == node->lit->occ_list.first);

      node->lit->occ_list.first = node->occ_link.next;

      if (node->occ_link.next)
        {
          node->occ_link.next->occ_link.prev = node->occ_link.prev;

          assert (!node->lit->occ_list.first->occ_link.prev);
        }
      else
        {                       /* node == first == last */
          assert (node == node->lit->occ_list.last);

          node->lit->occ_list.last = node->lit->occ_list.first;

          assert (!node->lit->occ_list.first);
          assert (!node->lit->occ_list.last);
        }
    }

  node->occ_link.next = node->occ_link.prev = 0;

  assert (node->lit->occ_cnt);

  node->lit->occ_cnt--;

  if (node->lit->occ_cnt == 0)
    {
      collect_variable_as_unate (nenofex, node->lit->var);
    }
#ifndef NDEBUG
  else if (node->lit->occ_cnt == 1)
    {
      assert (node->lit->occ_list.first);
      assert (node->lit->occ_list.first == node->lit->occ_list.last);
    }
  else
    {
      assert (node->lit->occ_list.first);
      assert (node->lit->occ_list.first != node->lit->occ_list.last);
    }
#endif
}


/* 
- unlink node from child-list
*/
void
unlink_node (Nenofex * nenofex, Node * node)
{
  assert (node);

  if (!node->parent)
    {                           /* should NOT be called on graph_root */
      assert (node == nenofex->graph_root);
      assert (0);
    }

  assert (node->parent);
  assert (node->parent->child_list.first);
  assert (node->parent->child_list.last);

  if (node->level_link.prev)
    {
      node->level_link.prev->level_link.next = node->level_link.next;

      if (node->level_link.next)
        node->level_link.next->level_link.prev = node->level_link.prev;
      else
        {                       /* node == last */
          assert (node == node->parent->child_list.last);

          node->parent->child_list.last = node->level_link.prev;

          assert (!node->parent->child_list.last->level_link.next);
        }
    }
  else
    {                           /* node == first */
      assert (node == node->parent->child_list.first);

      node->parent->child_list.first = node->level_link.next;

      if (node->level_link.next)
        {
          node->level_link.next->level_link.prev = node->level_link.prev;

          assert (!node->parent->child_list.first->level_link.prev);
        }
      else
        {                       /* node == first == last */

          assert (node == node->parent->child_list.last);

          node->parent->child_list.last = node->parent->child_list.first;

          assert (!node->parent->child_list.first);
          assert (!node->parent->child_list.last);
        }
    }

  node->parent->num_children--;
  node->parent = node->level_link.next = node->level_link.prev = 0;
}


/* 
- after parent-merging: subtracts 'delta' from level of all nodes under 'root'
*/
static void
merge_parent_update_level (Node * root, unsigned const int delta)
{
  assert (!is_literal_node (root));
  assert (delta == 1 || delta == 2);

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      cur->level -= delta;

      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while */

  delete_stack (stack);
}


/* 
- starts at 'root' and goes up via parent-pointers until graph root is reached
- updates sizes of each node on path by adding 'delta' 
- called after inserting / deleting nodes from child-list of 'root'
*/
void
update_size_subformula (Nenofex * nenofex, Node * root, const int delta)
{
  assert (root);
  assert (delta);

  Node *cur = root;
  do
    {
      cur->size_subformula += delta;
      assert ((int) cur->size_subformula > 0);
    }
  while ((cur = cur->parent));
}


#if RESTRICT_ATPG_FAULT_NODE_SET
/*
- see description in header file
*/
static void
atpg_mark_nodes_for_testing_again (Nenofex * nenofex, Node * root)
{
  assert (!nenofex->atpg_rr_abort);
  assert (!nenofex->atpg_rr_reset_changed_subformula);

  unsigned int inverted_cur_mark =
    !nenofex->atpg_rr->global_atpg_test_node_mark;
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  if (root == nenofex->changed_subformula.lca)
    {
      if (!node_taken_from_fault_queue (root))
        root->atpg_info->next_atpg_test_node_mark = inverted_cur_mark;
      else
        root->atpg_info->cur_atpg_test_node_mark = inverted_cur_mark;

      Node **ch, *child;
      for (ch = nenofex->changed_subformula.children; (child = *ch); ch++)
        push_stack (stack, child);
    }
  else
    push_stack (stack, root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (!node_taken_from_fault_queue (cur))
        cur->atpg_info->next_atpg_test_node_mark = inverted_cur_mark;
      else
        cur->atpg_info->cur_atpg_test_node_mark = inverted_cur_mark;

      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);
}

#endif /* end if RESTRICT_ATPG_FAULT_NODE_SET */


static void unlink_variable_from_lca_list (Var * var);


static void
reset_lca_object (Nenofex * nenofex, Var * var, LCAObject * lca_object,
                  unsigned const int clean_up_lca_child_list_occs);


/*
- if node is deleted which is LCA of at least one var -> var-list must be cleared
- variables are marked for update
*/
static void
delete_lca_reset_and_mark_variables (Nenofex * nenofex, Node * var_lca)
{
  assert (var_lca->var_lca_list.first);

  Var *var, *next;
  for (var = var_lca->var_lca_list.first; var; var = next)
    {
      next = var->same_lca_link.next;
      unlink_variable_from_lca_list (var);
      reset_lca_object (nenofex, var, &var->exp_costs.lca_object, 1);
      /* NOTE: in this case, can we fully clean lca-child-list-occs? */

      lca_update_mark (var);
      inc_score_update_mark (var);
      dec_score_update_mark (var);
      collect_variable_for_update (nenofex, var);
    }                           /* end: for all variables for which 'parent' is lca */
}


/*
- TODO: clarify failed assertions
- no problems encountered so far...
*/
static void
delete_node_remove_from_lca_child_lists (Nenofex * nenofex, Node * parent,
                                         Node * node)
{
  assert (parent->var_lca_list.first);
  assert (node);

  Stack *occs = node->lca_child_list_occs;
  /*FAILED, but can happen   --assert(lca_child_list_occs); */

  /* NOTE: above assertion failed because during relinking 
     nodes in expansion, we do NOT clean up var-entries... */

  if (!occs)
    return;                     /* BUG FIX */

  /* FAILED; happens if stack has been cleaned up before .g. during unates-elim.   
     --assert(count_stack(lca_child_list_occs)); */

  void **v_var;
#if !DELETE_EMPTY_STACKS_IN_NODES
  for (v_var = occs->elems; v_var != occs->top;)
#else
  for (v_var = occs->elems;
       (occs = node->lca_child_list_occs) && v_var != occs->top;)
#endif
    {                           /* function 'reset_lca_object' will move 'top' towards 'elems' */
      Var *var = *v_var;

      unlink_variable_from_lca_list (var);
      reset_lca_object (nenofex, var, &var->exp_costs.lca_object, 1);

      lca_update_mark (var);
      inc_score_update_mark (var);
      dec_score_update_mark (var);
      collect_variable_for_update (nenofex, var);
    }                           /* end: for all vars where 'node' occurs in LCA-child-list */

#if DELETE_EMPTY_STACKS_IN_NODES
  assert (!node->lca_child_list_occs);
  assert (!node->pos_in_lca_children);
#endif
}


static void
replace_changed_lca_child (Nenofex * nenofex, Node * old_node,
                           Node * new_node);


static void remove_changed_lca_child (Nenofex * nenofex, Node * node);


#ifndef NDEBUG
static void assert_pos_in_changed_ch_list (Nenofex * nenofex);
#endif


static void
delete_node (Nenofex * nenofex, Node * node)
{
  Stack *occs = node->lca_child_list_occs;

  if (occs)
    {
      /* start: same code as in 'delete_node_remove_from_lca_child_list' */
      void **v_var;
#if !DELETE_EMPTY_STACKS_IN_NODES
      for (v_var = occs->elems; v_var != occs->top;)
#else
      for (v_var = occs->elems;
           (occs = node->lca_child_list_occs) && v_var != occs->top;)
#endif
        {                       /* function 'reset_lca_object' will move 'top' towards 'elems' */
          Var *var = *v_var;

          unlink_variable_from_lca_list (var);
          reset_lca_object (nenofex, var, &var->exp_costs.lca_object, 1);

          lca_update_mark (var);
          inc_score_update_mark (var);
          dec_score_update_mark (var);
          collect_variable_for_update (nenofex, var);
        }                       /* end: for all vars where 'node' occurs in LCA-child-list */
      /* end: same code */

#if !DELETE_EMPTY_STACKS_IN_NODES
      delete_stack (occs);
      assert (node->pos_in_lca_children);
      delete_stack (node->pos_in_lca_children);
#else
      assert (!node->lca_child_list_occs);
      assert (!node->pos_in_lca_children);
#endif
    }
  else
    {
      assert (!node->pos_in_lca_children);
    }

  mem_free (node, sizeof (Node));
}


/* 
- typically called after deleting a node from parent's children
- parent has only one child left
- the one remaining child is linked to its grand-parent
- if remaining child is op-node -> link its children to grand-parent
- several graph properties and data structures must be maintain
- TODO: refinements (introduce variables, ...)
*/
void
merge_parent (Nenofex * nenofex, Node * parent)
{
  assert (parent);
  assert (!is_literal_node (parent));
  assert (parent->child_list.first == parent->child_list.last);
  assert (!parent->child_list.first->level_link.prev);
  assert (!parent->child_list.first->level_link.next);

#if RESTRICT_ATPG_FAULT_NODE_SET
  unsigned int mark_nodes_for_testing_again = 0;
#endif

  LCAObject *changed_subformula = &(nenofex->changed_subformula);

  if (!parent->parent)
    {                           /* 'graph-root' to be set */
      assert (parent == nenofex->graph_root);
      assert (!parent->level_link.next);
      assert (!parent->level_link.prev);

      if (parent->atpg_info)
        mark_fault_node_as_deleted (parent->atpg_info->fault_node);

      if (parent == changed_subformula->lca &&
          !nenofex->atpg_rr_reset_changed_subformula)
        {

          if (nenofex->atpg_rr_called)
            {
              nenofex->atpg_rr_abort = 1;
            }

          assert (changed_subformula->num_children == 2);
          reset_changed_lca_object (nenofex);

          /* if 'changed' would become a single literal, then do nothing */

          if (!is_literal_node (parent->child_list.first))
            {
              nenofex->atpg_rr_abort = 0;

              changed_subformula->lca = parent->child_list.first;

              Node *child;
              for (child = changed_subformula->lca->child_list.first;
                   child; child = child->level_link.next)
                add_changed_lca_child (nenofex, child);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
              assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
              assert (changed_subformula->num_children >= 2);
              assert (changed_subformula->top_p ==
                      changed_subformula->children +
                      changed_subformula->num_children);
            }                   /* end: new lca is op-node */

        }                       /* end: parent is lca of 'changed_subformula' */
      else if (parent == changed_subformula->lca)
        {
          assert (nenofex->atpg_rr_abort);
          assert (nenofex->atpg_rr_called);
          nenofex->atpg_rr_reset_changed_subformula = 0;
          reset_changed_lca_object (nenofex);
        }

      nenofex->graph_root = parent->child_list.first;
      assert (nenofex->graph_root->parent);
      nenofex->graph_root->parent = 0;

      if (!is_literal_node (parent->child_list.first))
        merge_parent_update_level (parent->child_list.first, 1);
      else
        nenofex->graph_root->level = 0;

      if (parent->var_lca_list.first)
        {
          assert (parent->var_lca_list.last);
          delete_lca_reset_and_mark_variables (nenofex, parent);
        }

#if COMPUTE_NUM_DELETED_NODES
      nenofex->stats.total_deleted_nodes++;
      if (nenofex->atpg_rr_called)
        nenofex->stats.deleted_nodes_by_global_flow_redundancy++;
#endif

      delete_node (nenofex, parent);
      return;
    }

  /* grandparent has more than one child */
  assert (parent->parent->child_list.first !=
          parent->parent->child_list.last);

  /* unlink remaining child (or its children) and add to grandparent's ch-list */
  Node *sub_parent = parent->child_list.first;
  unlink_node (nenofex, parent->child_list.first);

  assert (!parent->child_list.first);
  assert (!parent->child_list.last);

  unsigned int lit_copied = 0;
  if (is_literal_node (sub_parent))
    {
#if RESTRICT_ATPG_FAULT_NODE_SET
      mark_nodes_for_testing_again = 1;
#endif

      /* 'sub_parent' is either lit in cur. subformula or 
         directly linked to node on path up to root */
      Var *var = sub_parent->lit->var;

      if (var->exp_costs.lca_object.lca)        /* BUG FIX */
        {
          dec_score_update_mark (var);
          collect_variable_for_update (nenofex, var);
        }

      /* since 'sub_parent' is literal, do not set new 'changed' */

      if (parent == changed_subformula->lca)
        {
          if (!nenofex->atpg_rr_reset_changed_subformula)
            {
              assert (changed_subformula->num_children == 2);
              reset_changed_lca_object (nenofex);

              if (nenofex->atpg_rr_called)
                nenofex->atpg_rr_abort = 1;
            }
          else
            {
              assert (nenofex->atpg_rr_abort);
              assert (nenofex->atpg_rr_called);
              changed_subformula->lca = parent->parent; /* move LCA in order to mark vars later */
            }
        }
      else if (parent->parent == changed_subformula->lca)
        {                       /* remove 'parent', add 'sub_parent' to changed-child list */
          assert (!nenofex->atpg_rr_reset_changed_subformula);

          replace_changed_lca_child (nenofex, parent, sub_parent);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
          assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
          assert (changed_subformula->num_children >= 2);
          assert (changed_subformula->top_p ==
                  changed_subformula->children +
                  changed_subformula->num_children);
        }
      else if (sub_parent == changed_subformula->lca)
        assert (0);             /* sub_parent is literal -> not allowed */

      add_node_to_child_list (nenofex, parent->parent, sub_parent);     /* sets level */
      assert (sub_parent->occ_link.next ||
              sub_parent->occ_link.prev
              || sub_parent == sub_parent->lit->occ_list.first);

      lit_copied = 1;

      if (parent->atpg_info)
        {
          assert (nenofex->atpg_rr_called);
          mark_fault_node_as_deleted (parent->atpg_info->fault_node);

          if (parent->parent->atpg_info && parent->parent->atpg_info->atpg_ch)
            push_stack (parent->parent->atpg_info->atpg_ch,
                        sub_parent->atpg_info->fault_node);
        }
    }
  else                          /* sub_parent is operator node */
    {
      /* NOTE: simplify checks by setting LCA to parent->parent first in all cases? */

      if (sub_parent == changed_subformula->lca)
        {                       /* sub_parent's children remain in child list, need to set new LCA 
                                   since children are copied up to grand-parent */
          assert (!nenofex->atpg_rr_reset_changed_subformula);

          if (nenofex->atpg_rr_called)
            nenofex->atpg_rr_abort = 1;
        }
      if (parent == changed_subformula->lca)
        {                       /* relink sub_parent's children */
          if (!nenofex->atpg_rr_reset_changed_subformula)
            {
              assert (changed_subformula->num_children == 2);
              reset_changed_lca_object (nenofex);
              changed_subformula->lca = parent; /* workaround - set flag for relinking */

              if (nenofex->atpg_rr_called)
                nenofex->atpg_rr_abort = 1;
            }
          else
            {
              assert (nenofex->atpg_rr_called);
              assert (nenofex->atpg_rr_abort);
              changed_subformula->lca = parent->parent;
            }
        }
      else if (parent->parent == changed_subformula->lca)
        {                       /* relink sub_parent's children */
          assert (!nenofex->atpg_rr_reset_changed_subformula);
          remove_changed_lca_child (nenofex, parent);
        }

      if (parent->atpg_info)
        {
          assert (nenofex->atpg_rr_called);
          mark_fault_node_as_deleted (parent->atpg_info->fault_node);
        }

      if (sub_parent->atpg_info)
        {
          assert (nenofex->atpg_rr_called);
          mark_fault_node_as_deleted (sub_parent->atpg_info->fault_node);
        }

      assert (sub_parent->type == parent->parent->type);
      assert (sub_parent->child_list.first != sub_parent->child_list.last);

      Node *child, *next;
      for (child = sub_parent->child_list.first; child; child = next)
        {
          next = child->level_link.next;

          if (!is_literal_node (child))
            merge_parent_update_level (child, 2);

          unlink_node (nenofex, child);
          add_node_to_child_list (nenofex, parent->parent, child);      /* levels are set */

          /* TODO: can handle literals early */
          if (is_literal_node (child))
            {
              assert (child->occ_link.next ||
                      child->occ_link.prev
                      || child == child->lit->occ_list.first);
              lit_copied = 1;

#if RESTRICT_ATPG_FAULT_NODE_SET
              mark_nodes_for_testing_again = 1;
#endif

              /* 'child' is either lit in cur. subformula (then var is already dec-score-marked)
                 or linked to node which is linked to path up to root */
              Var *var = child->lit->var;
              if (var->exp_costs.lca_object.lca)        /* BUG FIX */
                {
                  dec_score_update_mark (var);
                  collect_variable_for_update (nenofex, var);
                }
            }                   /* end: is literal */

#ifndef NDEBUG
          if (parent->parent->atpg_info)
            assert (nenofex->atpg_rr_called);
#endif

          /* push children on watcher stack */
          if (parent->parent->atpg_info && parent->parent->atpg_info->atpg_ch)
            push_stack (parent->parent->atpg_info->atpg_ch,
                        child->atpg_info->fault_node);

          /* NOTE: simplify check by setting lca to parent->parent first in all cases ? */
          if (!nenofex->atpg_rr_reset_changed_subformula &&
              (parent == changed_subformula->lca
               || parent->parent == changed_subformula->lca))
            add_changed_lca_child (nenofex, child);
        }                       /* end: for all children */

      if (!nenofex->atpg_rr_reset_changed_subformula &&
          (sub_parent == changed_subformula->lca
           || parent == changed_subformula->lca))
        {
          changed_subformula->lca = parent->parent;
#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
          assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
          assert (changed_subformula->num_children >= 2);
          assert (changed_subformula->top_p ==
                  changed_subformula->children +
                  changed_subformula->num_children);
        }

      if (sub_parent->var_lca_list.first)
        {
          assert (sub_parent->var_lca_list.last);
          delete_lca_reset_and_mark_variables (nenofex, sub_parent);
        }

#if COMPUTE_NUM_DELETED_NODES
      nenofex->stats.total_deleted_nodes++;
      if (nenofex->atpg_rr_called)
        nenofex->stats.deleted_nodes_by_global_flow_redundancy++;
#endif

      delete_node (nenofex, sub_parent);
    }                           /* end: sub_parent is operator node */

  /* unlink parent */
  Node *parent_parent = parent->parent;
  unlink_node (nenofex, parent);

  /* need to update watchers if parent->parent will not be reset afterwards */
  if (parent_parent->atpg_info)
    {
      assert (nenofex->atpg_rr_called);
      collect_assigned_node (nenofex->atpg_rr, parent_parent);

      if (parent_parent->atpg_info->atpg_ch)
        parent_parent->atpg_info->clean_up_watcher_list = 1;
    }

  if (parent->var_lca_list.first)
    {
      assert (parent->var_lca_list.last);
      delete_lca_reset_and_mark_variables (nenofex, parent);
    }


  if (parent_parent->var_lca_list.first)
    {
      assert (parent_parent->var_lca_list.last);
      delete_node_remove_from_lca_child_lists (nenofex, parent_parent,
                                               parent);
    }

#if COMPUTE_NUM_DELETED_NODES
  nenofex->stats.total_deleted_nodes++;
  if (nenofex->atpg_rr_called)
    nenofex->stats.deleted_nodes_by_global_flow_redundancy++;
#endif

  if (parent == nenofex->existential_split_or)
    nenofex->existential_split_or = 0;

  delete_node (nenofex, parent);


#if RESTRICT_ATPG_FAULT_NODE_SET
  if (mark_nodes_for_testing_again && !nenofex->atpg_rr_abort &&
      nenofex->atpg_rr_called && !nenofex->atpg_rr->global_flow_optimizing)
    atpg_mark_nodes_for_testing_again (nenofex, parent_parent);
#endif

  assert (!nenofex->atpg_rr_reset_changed_subformula
          || nenofex->atpg_rr_abort);

  if (lit_copied)
    {
      simplify_one_level (nenofex, parent_parent);
    }

  assert (!nenofex->changed_subformula.lca ||
          !is_literal_node (nenofex->changed_subformula.lca));
}


/* 
- unlinks the subformula rooted at 'root' from its parent's child-list 
- recursively frees all nodes; merge nodes if parent has 1 child left after del.
- several properties and data structures must be maintained within this function
- TODO: refinements (introduce variables, ...)
*/
void
remove_and_free_subformula (Nenofex * nenofex, Node * root)
{
#if COMPUTE_NUM_DELETED_NODES
  nenofex->stats.total_deleted_nodes += root->size_subformula;
  if (nenofex->atpg_rr_called)
    nenofex->stats.deleted_nodes_by_global_flow_redundancy +=
      root->size_subformula;
#endif

  Node *parent = root->parent;
  LCAObject *changed_subformula = &(nenofex->changed_subformula);

  assert (!nenofex->changed_subformula.lca ||
          nenofex->changed_subformula.lca->level <= root->level);

  if (root == nenofex->changed_subformula.lca)
    {
      if (!nenofex->atpg_rr_called)
        {
          assert (!is_literal_node (root));
          reset_changed_lca_object (nenofex);
        }
      else                      /* remember root's parent for marking variables */
        {
          nenofex->atpg_rr_abort = 1;
          nenofex->atpg_rr_reset_changed_subformula = 1;
          reset_changed_lca_object (nenofex);   /* BUG FIX */
          nenofex->changed_subformula.lca = parent;
        }
    }

  /* sufficient check: no successor of 'root' can equal 'existential_split_or' */
  if (root == nenofex->existential_split_or)
    nenofex->existential_split_or = 0;

  if (!parent)
    {                           /* handle deletion of graph-root */
      assert (root == nenofex->graph_root ||
              nenofex->distributivity_deleting_redundancies);
      goto FREE_GRAPH;
    }

  unlink_node (nenofex, root);

  int update_size_delta = root->size_subformula;

  assert (parent->child_list.first);
  assert (parent->child_list.last);

  if (parent->child_list.first == parent->child_list.last)
    {
      update_size_delta++;

      if (!is_literal_node (parent->child_list.first))
        update_size_delta++;

      update_size_subformula (nenofex, parent, -update_size_delta);
      merge_parent (nenofex, parent);
    }
  else                          /*no merging necessary */
    {
      update_size_subformula (nenofex, parent, -update_size_delta);

      if (root->changed_ch_list_pos)
        {                       /* must remove 'root' from changed-child list */
          assert (parent == changed_subformula->lca);
          remove_changed_lca_child (nenofex, root);

          /* if only 1 child left -> new "changed-subformula" if it not a literal */
          if (changed_subformula->num_children == 1)
            {
              assert (*changed_subformula->children);   /* remaining child on first pos */
              assert (*(changed_subformula->children + 1) == 0);

              /* 'parent' remains in graph, but must be excluded from testing */
              if (nenofex->atpg_rr_called)
                parent->atpg_info->fault_node->skip = 1;

              Node *remaining_child = *changed_subformula->children;

              reset_changed_lca_object (nenofex);

              if (!is_literal_node (remaining_child))
                {
                  changed_subformula->lca = remaining_child;
                  Node *ch;
                  for (ch = remaining_child->child_list.first; ch;
                       ch = ch->level_link.next)
                    add_changed_lca_child (nenofex, ch);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
                  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
                  assert (changed_subformula->num_children >= 2);
                  assert (changed_subformula->top_p ==
                          changed_subformula->children +
                          changed_subformula->num_children);
                }
              else if (nenofex->atpg_rr_called)
                {
                  nenofex->atpg_rr_abort = 1;
                  changed_subformula->lca = parent;
                  nenofex->atpg_rr_reset_changed_subformula = 1;
                }

            }                   /* end: only one child remaining */
        }                       /* end: 'root' is child of 'changed-subformula' */

      if (parent->var_lca_list.first)
        {                       /* 'root' possibly occurs in lca-ch list of a variable 
                                   -> must initialize such variables from scratch */
          assert (parent->var_lca_list.last);
          delete_node_remove_from_lca_child_lists (nenofex, parent, root);
        }                       /* end: parent is lca of a variable */

      /* need to update watchers if parent will not be reset afterwards */
      if (parent->atpg_info)
        {
          assert (nenofex->atpg_rr_called);
          collect_assigned_node (nenofex->atpg_rr, parent);

          if (parent->atpg_info->atpg_ch)
            parent->atpg_info->clean_up_watcher_list = 1;
        }
    }                           /* end: no merging necessary */

  if (is_literal_node (root))
    {
      unlink_node_from_occ_list (nenofex, root);

      assert (!root->occ_link.next);
      assert (!root->occ_link.prev);

      Var *var = root->lit->var;
      if (var != nenofex->cur_expanded_var)
        {
          lca_update_mark (var);
          inc_score_update_mark (var);
          dec_score_update_mark (var);
          collect_variable_for_update (nenofex, var);
        }

#ifndef NDEBUG
      if (root == changed_subformula->lca)
        assert (0);
#endif

      if (root->atpg_info)
        mark_fault_node_as_deleted (root->atpg_info->fault_node);

      if (root->var_lca_list.first)
        {                       /* CONJECTURE: should never occur if unates eliminated until saturation */
          Var *var = root->lit->var;

          assert (root->var_lca_list.first == root->var_lca_list.last);
          assert (root->var_lca_list.first == var);
          /* var has exactly one occ */
          assert (!var->lits[0].occ_list.first ||
                  var->lits[0].occ_list.first == var->lits[0].occ_list.last);
          assert (!var->lits[1].occ_list.first ||
                  var->lits[1].occ_list.first == var->lits[1].occ_list.last);
          assert (!var->lits[0].occ_list.first
                  || !var->lits[1].occ_list.first);
          assert (!var->lits[1].occ_list.first
                  || !var->lits[0].occ_list.first);

          unlink_variable_from_lca_list (var);
          reset_lca_object (nenofex, var,
                            &root->lit->var->exp_costs.lca_object, 1);
        }

      delete_node (nenofex, root);
      return;
    }                           /* end: root is literal */

FREE_GRAPH:;

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (is_literal_node (cur))
        {
          unlink_node_from_occ_list (nenofex, cur);

          Var *var = cur->lit->var;

          if (var != nenofex->cur_expanded_var)
            {
              lca_update_mark (var);
              inc_score_update_mark (var);
              dec_score_update_mark (var);
              collect_variable_for_update (nenofex, var);
            }

          if (cur->atpg_info)
            mark_fault_node_as_deleted (cur->atpg_info->fault_node);

          if (cur->var_lca_list.first)
            {                   /* CONJECTURE: should never occur if unates eliminated until saturation */
              assert (cur->var_lca_list.first == cur->var_lca_list.last);
              assert (cur->var_lca_list.first == var);
              assert (var->exp_costs.lca_object.lca);

              unlink_variable_from_lca_list (var);
              reset_lca_object (nenofex, var, &var->exp_costs.lca_object, 1);
            }

          delete_node (nenofex, cur);
        }
      else                      /* 'cur' is operator node */
        {
          Node *ch;
          for (ch = cur->child_list.last; ch; ch = ch->level_link.prev)
            {
              push_stack (stack, ch);
            }

          if (cur->atpg_info)
            mark_fault_node_as_deleted (cur->atpg_info->fault_node);

          if (cur->var_lca_list.first)
            {
              assert (cur->var_lca_list.last);
              Var *var, *next;
              for (var = cur->var_lca_list.first; var; var = next)
                {
                  next = var->same_lca_link.next;
                  unlink_variable_from_lca_list (var);
                  reset_lca_object (nenofex, var, &var->exp_costs.lca_object,
                                    1);
                  /* NOTE: in this case, need NOT clean lca-child-list-occs? */
                }               /* end: for all variables for which 'parent' is lca */
            }

          delete_node (nenofex, cur);
        }                       /* end: 'cur' is operator node */
    }                           /* end: while stack not empty */
  delete_stack (stack);

  if (!parent && !nenofex->distributivity_deleting_redundancies)
    {
      nenofex->graph_root = 0;
    }

  assert (!nenofex->atpg_rr_reset_changed_subformula
          || nenofex->atpg_rr_abort);
  assert (!nenofex->changed_subformula.lca
          || !is_literal_node (nenofex->changed_subformula.lca));
}


/*
- used during LCA-computation
*/
static void
unmark_lca_children (LCAObject * lca_object)
{
  Node **lca_child, *child;
  for (lca_child = lca_object->children; (child = *lca_child); lca_child++)
    {
      if (!is_literal_node (child))
        {
          assert (lca_child_marked (child));
          unmark_lca_child (child);
        }
    }                           /* end: for */
}


#ifndef NDEBUG
/*
- for assertion checking only
*/
static void
assert_all_lca_children_unmarked (LCAObject * lca_object)
{
  Node **lca_child;
  for (lca_child = lca_object->children; *lca_child; lca_child++)
    {
      if (!is_literal_node ((*lca_child)))
        {
          assert (!lca_child_marked (*lca_child));
        }
    }
}
#endif /* end ifndef NDEBUG */


static void
init_lca_object (LCAObject * lca_object)
{
  assert (LCA_CHILDREN_INIT_SIZE > 0);

  lca_object->lca = 0;
  lca_object->num_children = 0;
  lca_object->size_children = LCA_CHILDREN_INIT_SIZE;

  size_t bytes = LCA_CHILDREN_INIT_SIZE * sizeof (Node *);
  lca_object->children = (Node **) mem_malloc (bytes);
  assert (lca_object->children);
  memset (lca_object->children, 0, bytes);

  lca_object->top_p = lca_object->children;
}


static void
free_lca_children (LCAObject * lca_object)
{
  mem_free (lca_object->children,
            lca_object->size_children * sizeof (Node *));
  lca_object->children = 0;
}


static void
enlarge_lca_children (LCAObject * lca_object)
{
  assert (lca_object->num_children == lca_object->size_children);
  assert (lca_object->top_p ==
          (lca_object->children + lca_object->size_children));

  unsigned int old_size = lca_object->size_children;
  lca_object->size_children *= 2;

  lca_object->children =
    (Node **) mem_realloc (lca_object->children, old_size * sizeof (Node *),
                           lca_object->size_children * sizeof (Node *));
  assert (lca_object->children);
  memset (lca_object->children + old_size, 0,
          (lca_object->size_children - old_size) * sizeof (Node *));

  lca_object->top_p = lca_object->children + lca_object->num_children;

  assert (!*(lca_object->top_p));
  assert (lca_object->top_p ==
          (lca_object->children + lca_object->num_children));
}


static void
add_lca_child (LCAObject * lca_object, Node * child)
{
  assert (lca_object->num_children < lca_object->size_children);
  assert (!*(lca_object->top_p));

  *(lca_object->top_p) = child;

  lca_object->num_children++;
  lca_object->top_p++;

  if (lca_object->num_children == lca_object->size_children)
    enlarge_lca_children (lca_object);

}


static void
clear_lca_children (LCAObject * lca_object)
{
  unmark_lca_children (lca_object);
  memset (lca_object->children, 0,
          lca_object->num_children * sizeof (Node *));
  lca_object->num_children = 0;
  lca_object->top_p = lca_object->children;
}


#ifndef NDEBUG
void
assert_all_lca_occ_pos_integrity (Nenofex * nenofex)
{
  void **v_scope;
  for (v_scope = nenofex->scopes->elems; v_scope < nenofex->scopes->top;
       v_scope++)
    {
      Scope *scope = (Scope *) * v_scope;
      void **v_var;
      for (v_var = scope->vars->elems; v_var < scope->vars->top; v_var++)
        {
          Var *var = (Var *) * v_var;
          Node **ch, *child;
          unsigned long int child_pos = 0;
          for (ch = var->exp_costs.lca_object.children; (child = *ch);
               ch++, child_pos++)
            {
              unsigned long int vpos_in_occs =
                (unsigned long int) *(var->pos_in_lca_child_list_occs->elems +
                                      child_pos);
              assert (var ==
                      (Var *) * (child->lca_child_list_occs->elems +
                                 vpos_in_occs));
              assert (child_pos ==
                      (unsigned long int) *(child->pos_in_lca_children->
                                            elems + vpos_in_occs));
            }
        }
    }
}
#endif


static void
remove_lca_child_list_occ (Nenofex * nenofex, Node * child,
                           unsigned long int child_pos, Var * var)
{
  Stack *child_lca_child_list_occs = child->lca_child_list_occs;
  Stack *child_pos_in_lca_children = child->pos_in_lca_children;
#ifndef NDEBUG
  Node **var_lca_children = var->exp_costs.lca_object.children;
#endif
  Stack *var_pos_in_lca_child_list_occs = var->pos_in_lca_child_list_occs;

  assert (count_stack (child_lca_child_list_occs) &&
          count_stack (child_lca_child_list_occs) ==
          count_stack (child_pos_in_lca_children));
  assert (var->exp_costs.lca_object.num_children &&
          var->exp_costs.lca_object.num_children ==
          count_stack (var_pos_in_lca_child_list_occs));
  assert (var_pos_in_lca_child_list_occs->elems + child_pos <
          var_pos_in_lca_child_list_occs->end);

  unsigned long int var_pos = (unsigned long int)
    *(var_pos_in_lca_child_list_occs->elems + child_pos);

  assert (child_lca_child_list_occs->elems + var_pos <
          child_lca_child_list_occs->end);
  assert ((Var *) * (child_lca_child_list_occs->elems + var_pos) == var);
  assert (child_pos < var->exp_costs.lca_object.num_children);
  assert (*(var_lca_children + child_pos) == child);
  assert (child_pos_in_lca_children->elems + var_pos <
          child_pos_in_lca_children->end);
  assert ((unsigned long int)
          *(child_pos_in_lca_children->elems + var_pos) == child_pos);

  void **v_var = child_lca_child_list_occs->elems + var_pos;
  assert (v_var < child_lca_child_list_occs->top);
  assert (var == (Var *) * v_var);

  /* cut off */
  child_lca_child_list_occs->top--;
  child_pos_in_lca_children->top--;

  if (v_var != child_lca_child_list_occs->top)
    {                           /* was not last element: overwrite and restore position info */
      assert (count_stack (child_lca_child_list_occs));
      void **ch_pos_in_lca_ch_elems = child_pos_in_lca_children->elems;
      unsigned int other_var_child_pos_index =
        count_stack (child_lca_child_list_occs);
      unsigned long int other_var_child_pos =
        (unsigned long int) *(ch_pos_in_lca_ch_elems +
                              other_var_child_pos_index);

      *v_var = *child_lca_child_list_occs->top; /* overwrite by  last elems */
      *(ch_pos_in_lca_ch_elems + var_pos) =
        *(ch_pos_in_lca_ch_elems + count_stack (child_pos_in_lca_children));

      Var *other_var = *v_var;
      assert (*(other_var->exp_costs.lca_object.children +
                other_var_child_pos) == child);
      assert ((unsigned long int)
              *(other_var->pos_in_lca_child_list_occs->elems +
                other_var_child_pos) == other_var_child_pos_index);
      *(other_var->pos_in_lca_child_list_occs->elems + other_var_child_pos) =
        (void *) var_pos;
    }
#if DELETE_EMPTY_STACKS_IN_NODES
  else if (child_lca_child_list_occs->top == child_lca_child_list_occs->elems)
    {                           /* both stacks are empty -> delete */
      delete_stack (child_lca_child_list_occs);
      child->lca_child_list_occs = 0;
      delete_stack (child_pos_in_lca_children);
      child->pos_in_lca_children = 0;
    }

#endif
}


static void
reset_lca_object (Nenofex * nenofex, Var * var, LCAObject * lca_object,
                  unsigned const int clean_up_lca_child_list_occs)
{
  assert (&var->exp_costs.lca_object == lca_object);

  if (!clean_up_lca_child_list_occs)
    memset (lca_object->children, 0,
            lca_object->num_children * sizeof (Node *));
  else
    {
      unsigned long int child_pos = 0;
      Node **ch, *child;
      for (ch = lca_object->children; (child = *ch); ch++, child_pos++)
        {
          remove_lca_child_list_occ (nenofex, child, child_pos, var);
          *ch = 0;
        }                       /* end: for all LCA-children */
    }                           /* end: remove 'var' from 'lca-child-list-occs' */

  lca_object->num_children = 0;
  lca_object->top_p = lca_object->children;
  lca_object->lca = 0;

#ifndef NDEBUG
#if ASSERT_ALL_LCA_OCC_POS_INTEGRITY
  assert_all_lca_occ_pos_integrity (nenofex);
#endif
#endif
}


/* 
- find root of smallest subformula which contains both node 'a' and node 'b'
- collects root's children which contain a,b (since 'root' is n-ary)
*/
static void
find_lca_and_children (Nenofex * nenofex, Node * a, Node * b,
                       LCAObject * lca_object)
{
  assert (a || b);
  assert (!lca_object->lca || lca_object->lca == a || lca_object->lca == b);

#if COMPUTE_LCA_PARENT_VISITS
  nenofex->stats.num_total_lca_algo_calls++;
#endif

  if (!a)
    {
      lca_object->lca = b;
      return;
    }

  if (!b || a == b)
    {
      lca_object->lca = a;
      return;
    }

#ifndef NDEBUG
  if (a != b)
    {
      assert (a->parent || b->parent);
      assert (b->parent || a->parent);
    }
#endif

  Node *high_node, *low_node, *high_node_prev, *low_node_prev;

  low_node_prev = 0;

  if (a->level >= b->level)
    {
      high_node = b;
      low_node = a;
    }
  else
    {
      high_node = a;
      low_node = b;
    }

  unsigned const int high_node_level = high_node->level;

  while (high_node_level < low_node->level)
    {
#if COMPUTE_LCA_PARENT_VISITS
      nenofex->stats.num_total_lca_parent_visits++;
#endif
      low_node_prev = low_node;
      low_node = low_node->parent;
    }

  if (high_node == low_node)
    {
      if (!lca_child_marked (low_node_prev)
          && !is_literal_node (low_node_prev))
        {
          mark_lca_child (low_node_prev);
          add_lca_child (lca_object, low_node_prev);
        }
      else if (is_literal_node (low_node_prev))
        add_lca_child (lca_object, low_node_prev);
    }
  else
    {                           /* high != low -> move up successively in parallel */
      do
        {
#if COMPUTE_LCA_PARENT_VISITS
          nenofex->stats.num_total_lca_parent_visits += 2;
#endif
          low_node_prev = low_node;
          low_node = low_node->parent;

          high_node_prev = high_node;
          high_node = high_node->parent;

          assert (high_node);
          assert (low_node);
        }
      while (high_node != low_node);
      assert (high_node);

      /* clear and add both children */
      clear_lca_children (lca_object);

      if (!is_literal_node (low_node_prev))
        {
          assert (!lca_child_marked (low_node_prev));
          mark_lca_child (low_node_prev);
        }
      add_lca_child (lca_object, low_node_prev);

      if (!is_literal_node (high_node_prev))
        {
          assert (!lca_child_marked (high_node_prev));
          mark_lca_child (high_node_prev);
        }
      add_lca_child (lca_object, high_node_prev);

    }

  lca_object->lca = high_node;
}


#ifndef NDEBUG
/* 
- used in assertion checking only 
*/
static Node *
node_has_child (Node * node, Node * child)
{
  Node *cur;
  for (cur = node->child_list.first;
       cur && cur != child; cur = cur->level_link.next)
    ;
  return cur;
}


static void
assert_lca_object_integrity (Nenofex * nenofex,
                             LCAObject * lca_object, Var * var)
{
  assert (lca_object == &(var->exp_costs.lca_object));

  assert (lca_object);
  assert (lca_object->lca);

  if (is_literal_node (lca_object->lca))
    {
      assert (lca_object->num_children == 0);
      assert (lca_object->children == lca_object->top_p);
      assert (*lca_object->top_p == 0);
      return;
    }

  Node **cur_p;
  /* each collected child has to contain either pos. or neg. lit, 
     and is marked if is an OP-node */
  for (cur_p = lca_object->children; *cur_p; cur_p++)
    {
      if (!is_literal_node ((*cur_p)))
        assert (lca_child_marked (*cur_p));

      assert (node_has_child (lca_object->lca, *cur_p));
      assert (find_node_by_id (*cur_p, var->id)
              || find_node_by_id (*cur_p, -var->id));
    }

  Node *cur;
  /* all occs of a vars must be contained in subformula of some child of lca */
  for (cur = var->lits[0].occ_list.first; cur; cur = (cur)->occ_link.next)
    {
      Node **child, *found;
      found = 0;
      for (child = lca_object->children; *child && !found; child++)
        {
          found = find_node_by_address (*child, cur);
        }
      assert (found);
    }
  for (cur = var->lits[1].occ_list.first; cur; cur = (cur)->occ_link.next)
    {
      Node **child, *found;
      found = 0;
      for (child = lca_object->children; *child && !found; child++)
        {
          found = find_node_by_address (*child, cur);
        }
      assert (found);
    }

  /* each collected child occurs exactly once in list */
  unsigned int i;
  for (i = 0; lca_object->children[i]; i++)
    {
      unsigned int j;
      for (j = 0; j < i; j++)
        {
          assert (lca_object->children[i] != lca_object->children[j]);
        }
      for (j = i + 1; lca_object->children[j]; j++)
        {
          assert (lca_object->children[i] != lca_object->children[j]);
        }
    }

  assert (is_literal_node (lca_object->lca) || lca_object->num_children >= 2);
  assert (!is_literal_node (lca_object->lca)
          || lca_object->num_children == 0);
}
#endif /* end: ifndef NDEBUG */


/*
- add a variable to LCA-list of its LCA
*/
static void
add_variable_to_lca_list (Nenofex * nenofex, Var * var)
{
  Node *lca = var->exp_costs.lca_object.lca;

  assert (lca);
  /* var must have been properly unlinked before */
  assert (!var->same_lca_link.prev);
  assert (!var->same_lca_link.next);

  if (!lca->var_lca_list.first)
    {                           /* add to empty list */
      assert (!lca->var_lca_list.last);

      lca->var_lca_list.first = lca->var_lca_list.last = var;
    }
  else                          /* append */
    {
      assert (lca->var_lca_list.last);
      assert (lca->var_lca_list.first == lca->var_lca_list.last ||
              lca->var_lca_list.last->same_lca_link.prev);
      assert (lca->var_lca_list.first != lca->var_lca_list.last ||
              !lca->var_lca_list.last->same_lca_link.prev);
      assert (!lca->var_lca_list.last->same_lca_link.next);

      lca->var_lca_list.last->same_lca_link.next = var;
      var->same_lca_link.prev = lca->var_lca_list.last;
      lca->var_lca_list.last = var;
    }
}


#if 1
/*
- unlink a variable from LCA-list of its LCA
*/
static void
unlink_variable_from_lca_list (Var * var)
{
  Node *lca = var->exp_costs.lca_object.lca;

  assert (lca);
  assert (var != lca->var_lca_list.first || !var->same_lca_link.prev);
  assert (var == lca->var_lca_list.first || var->same_lca_link.prev);
  assert (var != lca->var_lca_list.last || !var->same_lca_link.next);
  assert (var == lca->var_lca_list.last || var->same_lca_link.next);

  if (var->same_lca_link.prev)
    {
      var->same_lca_link.prev->same_lca_link.next = var->same_lca_link.next;

      if (var->same_lca_link.next)
        {
          var->same_lca_link.next->same_lca_link.prev =
            var->same_lca_link.prev;
          var->same_lca_link.next = var->same_lca_link.prev = 0;
        }
      else
        {                       /* var == last */
          assert (var == lca->var_lca_list.last);

          lca->var_lca_list.last = var->same_lca_link.prev;
          var->same_lca_link.prev = 0;

          assert (!lca->var_lca_list.last->same_lca_link.next);
        }
    }
  else
    {                           /* var == first */
      assert (var == lca->var_lca_list.first);

      lca->var_lca_list.first = var->same_lca_link.next;

      if (var->same_lca_link.next)
        {
          var->same_lca_link.next->same_lca_link.prev =
            var->same_lca_link.prev;
          var->same_lca_link.next = 0;

          assert (!lca->var_lca_list.first->same_lca_link.prev);
        }
      else
        {                       /* var == first == last */
          assert (var == lca->var_lca_list.last);

          lca->var_lca_list.last = lca->var_lca_list.first;

          assert (!lca->var_lca_list.first);
          assert (!lca->var_lca_list.last);
        }
    }

  assert (!var->same_lca_link.prev);
  assert (!var->same_lca_link.next);
}
#endif


/* 
- add variable 'var' to 'lca-child-list-occs' of all LCA-children
- TODO: adapt initial stack size? 
*/
void
add_variable_to_lca_child_list_occs_of_nodes (Nenofex * nenofex,
                                              Var * var,
                                              LCAObject * lca_object)
{
  Stack *var_pos_in_lca_child_list_occs = var->pos_in_lca_child_list_occs;
  assert (var_pos_in_lca_child_list_occs);
  reset_stack (var_pos_in_lca_child_list_occs);

  unsigned long int child_pos = 0;
  Node **ch, *child;
  for (ch = lca_object->children; (child = *ch); ch++, child_pos++)
    {
      Stack *child_lca_child_list_occs = child->lca_child_list_occs;
      Stack *child_pos_in_lca_children = child->pos_in_lca_children;

      if (!child_lca_child_list_occs)
        {                       /* TODO: adapt initial stack size? */
          assert (!child_pos_in_lca_children);
          child->lca_child_list_occs =
            child_lca_child_list_occs = create_stack (DEFAULT_STACK_SIZE);
          child->pos_in_lca_children =
            child_pos_in_lca_children = create_stack (DEFAULT_STACK_SIZE);
        }
      assert (child_pos_in_lca_children);

      push_stack (var_pos_in_lca_child_list_occs,
                  (void *) (unsigned long int)
                  count_stack (child_lca_child_list_occs));

      /* push 'var' on occ-stack, 'pos' of child in lca-children on pos-stack */
      assert (count_stack (child_lca_child_list_occs) ==
              count_stack (child_pos_in_lca_children));
      push_stack (child_lca_child_list_occs, var);
      push_stack (child_pos_in_lca_children, (void *) child_pos);

      assert (count_stack (child->lca_child_list_occs) ==
              count_stack (child->pos_in_lca_children));
    }                           /* end: for all LCA-children */

  assert (lca_object->num_children ==
          count_stack (var->pos_in_lca_child_list_occs));

#ifndef NDEBUG
#if ASSERT_ALL_LCA_OCC_POS_INTEGRITY
  assert_all_lca_occ_pos_integrity (nenofex);
#endif
#endif
}


/* 
- computes LCA over occs of a var and collects LCA's children containing these occs
- insert variable into LCA's variable list
*/
static void
find_variable_lca_and_children (Nenofex * nenofex, Var * var,
                                LCAObject * lca_object,
                                const int add_var_entry)
{
  assert (var);
  assert (variable_has_occs (var));

#ifndef NDEBUG
#if ASSERT_BEFORE_FIND_VAR_LCA_FULL_GRAPH_INTEGRITY
  assert_all_occ_lists_integrity (nenofex);
  assert_all_child_occ_lists_integrity (nenofex);
#endif
#endif

  assert (!lca_object->lca);
  assert (lca_object->num_children == 0);
  assert (lca_object->size_children != 0);
  assert (lca_object->top_p == lca_object->children);
  assert (!var->same_lca_link.prev);
  assert (!var->same_lca_link.next);

  Node *cur;

  for (cur = var->lits[0].occ_list.first; cur; cur = cur->occ_link.next)
    {                           /* neg. occ's */
      find_lca_and_children (nenofex, lca_object->lca, cur, lca_object);
    }

  for (cur = var->lits[1].occ_list.first; cur; cur = cur->occ_link.next)
    {                           /* pos. occ's */
      find_lca_and_children (nenofex, lca_object->lca, cur, lca_object);
    }

#ifndef NDEBUG
#if ASSERT_LCA_OBJECT_INTEGRITY
  assert_lca_object_integrity (nenofex, lca_object, var);
#endif
#endif

  unmark_lca_children (lca_object);

  add_variable_to_lca_list (nenofex, var);

  if (add_var_entry)
    add_variable_to_lca_child_list_occs_of_nodes (nenofex, var, lca_object);

#ifndef NDEBUG
#if ASSERT_ALL_LCA_CHILDREN_UNMARKED
  assert_all_lca_children_unmarked (lca_object);
#endif
#endif

#ifndef NDEBUG
#if ASSERT_AFTER_FIND_VAR_LCA_FULL_GRAPH_INTEGRITY
  assert_all_occ_lists_integrity (nenofex);
  assert_all_child_occ_lists_integrity (nenofex);
#endif
#endif
}


/* START: LCA-COMPUTATION OF NON-INNERMOST UNIVERSAL VARIABLES */

static void
mark_universal_lca_children (LCAObject * universal_lca_obj)
{
  Node **ch, *child;
  for (ch = universal_lca_obj->children; (child = *ch); ch++)
    {
      assert (!universal_lca_child_marked (child));
      mark_universal_lca_child (child);
    }
}


static void
unmark_universal_lca_children (LCAObject * universal_lca_obj)
{
  Node **ch, *child;
  for (ch = universal_lca_obj->children; (child = *ch); ch++)
    {
      assert (universal_lca_child_marked (child));
      unmark_universal_lca_child (child);
    }
}


static void
collect_depending_variable (Stack * depending_existential_variables,
                            Var * var)
{
  if (!var->collected_as_depending)
    {
      var->collected_as_depending = 1;
      push_stack (depending_existential_variables, var);
    }
}


/*
- check lca-subformula of univ. var. and collect dep. vars. from innermost scope 
- VERY IMPORTANT that NO node is visited more than once during successive 
    calls of this function as typically happens during LCA-computation
*/
static void
collect_innermost_depending_existential_variables (Var * universal_var,
                                                   Stack *
                                                   depending_existential_variables,
                                                   Stack *
                                                   dependency_marked_nodes)
{
  LCAObject *universal_lca_object = &universal_var->exp_costs.lca_object;
  /* NOTE: assertion will fail if units are not eliminated until saturation */
  assert (universal_lca_object->lca
          && !is_literal_node (universal_lca_object->lca));

  const unsigned int universal_nesting = universal_var->scope->nesting;
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  Node **ch, *child;
  for (ch = universal_lca_object->children; (child = *ch); ch++)
    {
      if (!dependency_visit_marked (child))
        push_stack (stack, child);
    }

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (!dependency_visit_marked (cur));

      if (is_literal_node (cur))
        {                       /* collect exist. variables if depending on cur. universal variable */
          Var *var = cur->lit->var;
          Scope *scope = var->scope;

          if (is_existential_scope (scope)
              && scope->nesting > universal_nesting)
            collect_depending_variable (depending_existential_variables, var);
        }
      else                      /* op-node */
        {
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            {
              if (!dependency_visit_marked (child))
                push_stack (stack, child);
            }
        }                       /* end: op-node */
    }                           /* end: while stack not empty */

  delete_stack (stack);

  if (dependency_marked_nodes)
    {                           /* mark current LCA and LCA-children as visited */
#if 0
      /* BUG FIX */
      Node *universal_lca = universal_lca_object->lca;
      if (!dependency_visit_marked (universal_lca))
        {
          mark_dependency_visit (universal_lca);
          push_stack (dependency_marked_nodes, universal_lca);
        }
#endif

      for (ch = universal_lca_object->children; (child = *ch); ch++)
        {
          if (!dependency_visit_marked (child))
            {
              mark_dependency_visit (child);
              push_stack (dependency_marked_nodes, child);
            }
        }                       /* end: for all LCA-children */
    }                           /* end: mark nodes as visited */
}


static void
unify_universal_lca_children (LCAObject * universal_lca_object,
                              LCAObject * existential_lca_object)
{
  Node **ch, *child;
  for (ch = existential_lca_object->children; (child = *ch); ch++)
    {                           /* add children if not already contained */
      if (!universal_lca_child_marked (child))
        {
          mark_universal_lca_child (child);
          add_lca_child (universal_lca_object, child);
        }
    }                           /* end: for */
}


/*
- computes LCA of a non-innermost univ. var. by merging two given LCAs
- by this way the LCA of such a universal variable is computed incrementally
- LCA of each depending existential variable is passed onto this function
- NOTE: EXACTLY the same algorithm as for maintaining current 'changed-subformula'
*/
static void
compute_universal_lca_by_unification (Nenofex * nenofex,
                                      Var * universal_var,
                                      LCAObject * universal_lca_object,
                                      LCAObject * existential_lca_object)
{
  assert (&universal_var->exp_costs.lca_object == universal_lca_object);

#if COMPUTE_LCA_PARENT_VISITS
  nenofex->stats.num_total_lca_algo_calls++;
#endif

  Node *universal_lca = universal_lca_object->lca;
  Node *existential_lca = existential_lca_object->lca;

  assert (universal_lca);
  assert (existential_lca);

  if (universal_lca == existential_lca)
    {
      unify_universal_lca_children (universal_lca_object,
                                    existential_lca_object);
      return;
    }

  Node *high_node, *low_node, *high_node_prev, *low_node_prev;

  low_node_prev = 0;

  if (universal_lca->level >= existential_lca->level)
    {
      high_node = existential_lca;
      low_node = universal_lca;
    }
  else
    {
      high_node = universal_lca;
      low_node = existential_lca;
    }

  unsigned const int high_node_level = high_node->level;

  while (high_node_level < low_node->level)
    {                           /* check if high_node is reachable from low_node via parent-pointers */
#if COMPUTE_LCA_PARENT_VISITS
      nenofex->stats.num_total_lca_parent_visits++;
#endif
      low_node_prev = low_node;
      low_node = low_node->parent;
    }

  if (high_node == low_node)
    {                           /* check whether low_node is successor of an lca-child of 
                                   high_node or only of an "ordinary" child */

      if (high_node == universal_lca)
        {
          if (universal_lca_child_marked (low_node_prev))
            {                   /* nothing to be done: 'existential_lca' is contained in 'universal_lca' */
              ;
            }
          else
            {                   /* add child to universal_lca-children */
              mark_universal_lca_child (low_node_prev);
              add_lca_child (universal_lca_object, low_node_prev);
            }
        }
      else                      /* high_node == existential_lca */
        {                       /* set 'universal_lca := existential_lca' */
          assert (high_node == existential_lca);

          /* must reset temporary univ. LCA-object and assign exist. LCA-object */
          unmark_universal_lca_children (universal_lca_object);
          unlink_variable_from_lca_list (universal_var);
          reset_lca_object (nenofex, universal_var, universal_lca_object, 0);

          Node **ch, *child;
          for (ch = existential_lca_object->children; (child = *ch); ch++)
            {
              assert (!universal_lca_child_marked (child));
              mark_universal_lca_child (child);
              add_lca_child (universal_lca_object, child);
            }                   /* end: for */

          if (!universal_lca_child_marked (low_node_prev))
            {
              mark_universal_lca_child (low_node_prev);
              add_lca_child (universal_lca_object, low_node_prev);
            }

          universal_lca_object->lca = existential_lca;
          add_variable_to_lca_list (nenofex, universal_var);

        }                       /* end: high_node == existential_lca */

    }
  else
    {                           /* need to find 'lca(universal_lca, existential_lca)' by 
                                   following parent-pointers in parallel */

      do
        {
#if COMPUTE_LCA_PARENT_VISITS
          nenofex->stats.num_total_lca_parent_visits += 2;
#endif
          low_node_prev = low_node;
          low_node = low_node->parent;

          high_node_prev = high_node;
          high_node = high_node->parent;

          assert (high_node);
          assert (low_node);
        }
      while (high_node != low_node);
      assert (high_node);

      unmark_universal_lca_children (universal_lca_object);
      unlink_variable_from_lca_list (universal_var);
      reset_lca_object (nenofex, universal_var, universal_lca_object, 0);

      universal_lca_object->lca = high_node;

      assert (!universal_lca_child_marked (low_node_prev));
      mark_universal_lca_child (low_node_prev);
      add_lca_child (universal_lca_object, low_node_prev);

      assert (!universal_lca_child_marked (high_node_prev));
      mark_universal_lca_child (high_node_prev);
      add_lca_child (universal_lca_object, high_node_prev);

      add_variable_to_lca_list (nenofex, universal_var);
    }                           /* end: compute 'lca(universal_lca, existential_lca)' */

  assert (!is_literal_node (universal_lca_object->lca));
}


#ifndef NDEBUG
static void
assert_no_var_collected_as_depending (Nenofex * nenofex)
{
  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;

      void **v_var, **var_end;
      var_end = scope->vars->top;
      for (v_var = scope->vars->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;
          assert (!var->collected_as_depending);
        }                       /* end: for all vars */

    }                           /* end: for all scopes */
}
#endif /* end: ifndef NDEBUG */


#ifndef NDEBUG
static void
assert_universal_lca_computation_graph_and_vars (Nenofex * nenofex)
{
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (!lca_child_marked (cur));
      assert (!universal_lca_child_marked (cur));
      assert (!dependency_visit_marked (cur));

      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while stack not empty */
  delete_stack (stack);

  assert_no_var_collected_as_depending (nenofex);
}
#endif /* end: ifndef NDEBUG */


static void
init_variable_scores (Nenofex * nenofex, Var * var,
                      const int update_key_priority_queue);


/* only a minor optimization */
#define OPTIMIZED_VAR_COLLECTION 1

/*
- for expansions of universal variables from non-innermost scope
*/
static void
find_non_innermost_universal_lca_and_children (Nenofex * nenofex,
                                               Var * universal_var)
{
#ifndef NDEBUG
#if ASSERT_UNIVERSAL_LCA_COMPUTATION_GRAPH_AND_VARS
  assert_universal_lca_computation_graph_and_vars (nenofex);
#endif
#endif
  assert (nenofex->next_scope && is_universal_scope (*nenofex->next_scope));
  assert (nenofex->cur_scope && is_existential_scope (*nenofex->cur_scope));
  assert (is_universal_scope (universal_var->scope) &&
          universal_var->scope->nesting < (*nenofex->cur_scope)->nesting);

  LCAObject *universal_lca_object = &universal_var->exp_costs.lca_object;
  Stack *depending_existential_variables = create_stack (DEFAULT_STACK_SIZE);
  Stack *processed_existential_variables = create_stack (DEFAULT_STACK_SIZE);
  Stack *dependency_marked_nodes = create_stack (DEFAULT_STACK_SIZE);

  /* first compute lca of univ. var. in standard way, 
     but do NOT add var. to lca-children yet */
  find_variable_lca_and_children (nenofex, universal_var,
                                  universal_lca_object, 0);
  mark_universal_lca_children (universal_lca_object);
  collect_innermost_depending_existential_variables (universal_var,
                                                     depending_existential_variables,
                                                     dependency_marked_nodes);

  Var *depending_var;

#if OPTIMIZED_VAR_COLLECTION
  while (count_stack (depending_existential_variables))
    {
#endif

      while ((depending_var = pop_stack (depending_existential_variables)))
        {
          /* need to store processed vars for unmarking AFTERWARDS */
          push_stack (processed_existential_variables, depending_var);

          assert (is_existential_scope (depending_var->scope));
          assert (depending_var->scope->nesting >
                  universal_var->scope->nesting);

          LCAObject *existential_lca_object =
            &depending_var->exp_costs.lca_object;

          if (!existential_lca_object->lca)
            {                   /* compute LCA of depending var from scratch */
              find_variable_lca_and_children (nenofex, depending_var,
                                              existential_lca_object, 1);
            }
          else if (lca_update_marked (depending_var))
            {                   /* reset + compute lca of depending var. */
              unlink_variable_from_lca_list (depending_var);
              reset_lca_object (nenofex, depending_var,
                                existential_lca_object, 1);
              find_variable_lca_and_children (nenofex, depending_var,
                                              existential_lca_object, 1);
              lca_update_unmark (depending_var);
            }

          /* next, unify both LCAs to get extended universal LCA */
          compute_universal_lca_by_unification (nenofex, universal_var,
                                                universal_lca_object,
                                                existential_lca_object);

#if OPTIMIZED_VAR_COLLECTION
        }                       /* end: inner while: lca-computation  */
#endif

      /* now, 'universal_lca_object' holds current univ. LCA; 
         next, once again collect depending variables */
      collect_innermost_depending_existential_variables (universal_var,
                                                         depending_existential_variables,
                                                         dependency_marked_nodes);
    }                           /* end: outer while: while not all depending variables processed */

  /* univ. LCA has been computed; next, unmark lca-children 
     and add variable entries */
  unmark_universal_lca_children (universal_lca_object);
  add_variable_to_lca_child_list_occs_of_nodes (nenofex, universal_var,
                                                universal_lca_object);

  /* unmark collected depending variables */
  while ((depending_var = pop_stack (processed_existential_variables)))
    {
      assert (depending_var->collected_as_depending);
      depending_var->collected_as_depending = 0;
    }

  Node *node;
  while ((node = pop_stack (dependency_marked_nodes)))
    {
      assert (dependency_visit_marked (node));
      unmark_dependency_visit (node);
    }

  delete_stack (depending_existential_variables);
  delete_stack (processed_existential_variables);
  delete_stack (dependency_marked_nodes);

#ifndef NDEBUG
#if ASSERT_UNIVERSAL_LCA_COMPUTATION_GRAPH_AND_VARS
  assert_universal_lca_computation_graph_and_vars (nenofex);
#endif
#endif
}

/* END: LCA-COMPUTATION OF NON-INNERMOST UNIVERSAL VARIABLES */


#ifndef NDEBUG
/* 
- for assertion checking only
*/
static unsigned int
count_children (Nenofex * nenofex, Node * node)
{
  assert (node);

  unsigned int result = 0;

  if (is_literal_node (node))   /* literal might be marked so loop would be entered */
    return result;

  Node *child;
  for (child = node->child_list.first; child; child = child->level_link.next)
    result++;

  return result;
}
#endif /* end ifndef NDEBUG */


/*
- when copying literal nodes (universal expansion): must check whether var. 
    has been copied or not (i.e. depends on expanded univ. var.)
*/
static Node *
copy_node (Nenofex * nenofex, Node * node)
{
  Node *copy = 0;

  switch (node->type)
    {
    case NODE_TYPE_LITERAL:
      {
        assert (node->id == node->lit->negated ?
                -node->lit->var->id : node->lit->var->id);
        Var *var = node->lit->var;

        copy = lit_node (nenofex, node->id, var->copied ? var->copied : var);
        copy->size_subformula = node->size_subformula;
        /* level, parent and occ_links will be set when adding to an op-node */
        break;
      }
    case NODE_TYPE_AND:
      {
        copy = and_node (nenofex);
        copy->level = node->level;
        copy->id = node->id;
        copy->size_subformula = node->size_subformula;
        nenofex->next_free_node_id--;
        break;
      }
    case NODE_TYPE_OR:
      {
        copy = or_node (nenofex);
        copy->level = node->level;
        copy->id = node->id;
        copy->size_subformula = node->size_subformula;
        nenofex->next_free_node_id--;
        break;
      }
    default:
      assert (0);
    }                           /* end: switch */

  return copy;
}


#ifndef NDEBUG
/*
- for assertion checking only
*/
void
assert_copy_equals (Nenofex * nenofex, Node * original, Node * copy)
{
  Stack *stack1 = create_stack (16);
  Stack *stack2 = create_stack (16);
  push_stack (stack1, (void *) original);
  push_stack (stack2, (void *) copy);

  Node *cur1, *cur2;
  while ((cur1 = pop_stack (stack1)))
    {
      cur2 = pop_stack (stack2);
      assert (cur2);
      assert (cur1->type == cur2->type);
      assert (cur1->level == cur2->level);
      assert (cur1->num_children == cur2->num_children);
      assert (cur1->size_subformula == cur2->size_subformula);

      if (is_literal_node (cur1))
        {
          assert (cur1->id == cur2->id);
        }
      else
        {
          assert (cur1->id == cur2->id);
          Node *child;
          unsigned int cnt1, cnt2;
          cnt1 = cnt2 = 0;
          for (child = cur1->child_list.last; child;
               child = child->level_link.prev)
            {
              push_stack (stack1, (void *) child);
              cnt1++;
            }
          for (child = cur2->child_list.last; child;
               child = child->level_link.prev)
            {
              push_stack (stack2, (void *) child);
              cnt2++;
            }
          assert (cnt1 == cnt2);
        }
    }                           /* end: while */

  assert (!count_stack (stack1));
  assert (!count_stack (stack2));
  delete_stack (stack1);
  delete_stack (stack2);
}
#endif


static void
add_node_to_child_list_before (Nenofex * nenofex, Node * child,
                               Node * new_child);


static void
add_lit_node_to_occurrence_list (Nenofex * nenofex, Node * new_occ);


/* 
- called in order to copy a formula (subgraph)
- simple copying, no marking as in 'copy_formula_mark_propagation' 
*/
static Node *
copy_formula (Nenofex * nenofex, Node * root)
{
  Stack *node_stack = create_stack (DEFAULT_STACK_SIZE);        /* nodes to be visited */
  Stack *copy_stack = create_stack (DEFAULT_STACK_SIZE);        /* copied nodes */
  Node *result;

  push_stack (node_stack, root);

  Node *cur;
  while ((cur = pop_stack (node_stack)))
    {
      if (is_literal_node (cur))
        {
          assert (!cur->lit->var->copied);
          result = copy_node (nenofex, cur);
          push_stack (copy_stack, result);
        }
      else
        {                       /* op-node -> copy children first */
          if (!copy_formula_marked (cur))
            {
              copy_formula_mark (cur);
              push_stack (node_stack, cur);

              Node *child;
              for (child = cur->child_list.last; child;
                   child = child->level_link.prev)
                {
                  push_stack (node_stack, child);
                }
            }
          else                  /* all children have been copied -> create OP-node */
            {
              copy_formula_unmark (cur);
              result = copy_node (nenofex, cur);

              assert (!result->child_list.first);
              assert (result->child_list.first == result->child_list.last);

              Node *child = pop_stack (copy_stack);
              add_node_to_child_list (nenofex, result, child);

              if (is_literal_node (child))
                add_lit_node_to_occurrence_list (nenofex, child);

              unsigned int cnt = cur->num_children - 1;
              unsigned int i;

              for (i = 0; i < cnt; i++)
                {
                  child = pop_stack (copy_stack);
                  add_node_to_child_list_before (nenofex,
                                                 result->child_list.first,
                                                 child);

                  if (is_literal_node (child))
                    add_lit_node_to_occurrence_list (nenofex, child);
                }

              push_stack (copy_stack, result);
            }
        }                       /* end: is op-node */
    }                           /* end: while */

  result = pop_stack (copy_stack);
  assert (result);

  if (is_literal_node (result))
    {                           /* special case: root is a literal */
      assert (is_literal_node (root));

      result->level = root->level;
      add_lit_node_to_occurrence_list (nenofex, result);
    }

  assert (!count_stack (copy_stack));
  assert (!count_stack (node_stack));

  delete_stack (node_stack);
  delete_stack (copy_stack);

#ifndef NDEBUG
#if ASSERT_COPY_EQUALS
  assert_copy_equals (nenofex, root, result);
#endif
#endif

  return result;
}


/* 
- called in order to copy a formula (subgraph) during expansion
- copy subformula and mark literals for forthcoming propagation
- copying and marking has been merged in this function 
*/
static Node *
copy_formula_mark_propagation (Nenofex * nenofex, Node * root,
                               Var * expanded_var)
{
  Stack *node_stack = create_stack (DEFAULT_STACK_SIZE);        /* nodes to be visited */
  Stack *copy_stack = create_stack (DEFAULT_STACK_SIZE);        /* copied nodes */
  Node *result;

  push_stack (node_stack, root);

  Node *cur;
  while ((cur = pop_stack (node_stack)))
    {
      if (is_literal_node (cur))
        {
          result = copy_node (nenofex, cur);
          Var *var = result->lit->var;

          assert (!cur->lit->var->copied || cur->lit->var->copied == var);

          /* copied literals will be assigned 'true' in forthcoming propagation */
          if (var == expanded_var)
            truth_propagation_mark (result);
          else
            {
              lca_update_mark (var);
              inc_score_update_mark (var);
              dec_score_update_mark (var);
              collect_variable_for_update (nenofex, var);
            }

          push_stack (copy_stack, result);
        }
      else
        {                       /* op-node -> copy children first */
          if (!copy_formula_marked (cur))
            {
              copy_formula_mark (cur);
              push_stack (node_stack, cur);

              Node *child;
              for (child = cur->child_list.last; child;
                   child = child->level_link.prev)
                {
                  push_stack (node_stack, child);
                }
            }
          else                  /* all children have been copied -> create OP-node */
            {
              copy_formula_unmark (cur);
              result = copy_node (nenofex, cur);

              assert (!result->child_list.first);
              assert (result->child_list.first == result->child_list.last);

              Node *child = pop_stack (copy_stack);
              add_node_to_child_list (nenofex, result, child);

              if (is_literal_node (child))
                add_lit_node_to_occurrence_list (nenofex, child);

              unsigned int cnt = cur->num_children - 1;
              unsigned int i;

              for (i = 0; i < cnt; i++)
                {
                  child = pop_stack (copy_stack);
                  add_node_to_child_list_before (nenofex,
                                                 result->child_list.first,
                                                 child);

                  if (is_literal_node (child))
                    add_lit_node_to_occurrence_list (nenofex, child);
                }

              push_stack (copy_stack, result);
            }
        }                       /* end: is op-node */
    }                           /* end: while */

  result = pop_stack (copy_stack);
  assert (result);

  if (is_literal_node (result))
    {                           /* special case: root is a literal */
      assert (is_literal_node (root));

      result->level = root->level;
      add_lit_node_to_occurrence_list (nenofex, result);
    }

  assert (!count_stack (copy_stack));
  assert (!count_stack (node_stack));

  delete_stack (node_stack);
  delete_stack (copy_stack);

#ifndef NDEBUG
#if ASSERT_COPY_EQUALS
  assert_copy_equals (nenofex, root, result);
#endif
#endif

  return result;
}


/*
- traverse subformula rooted at 'root' and set levels
*/
void
update_level (Nenofex * nenofex, Node * root)
{
  assert (root);
  assert (root->parent);

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      cur->level = cur->parent->level + 1;
      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while */

  delete_stack (stack);
}


#ifndef NDEBUG

/*
- for assertion checking only
*/
static void
assert_expand_node_integrity (Nenofex * nenofex, Node * parent)
{
  assert (parent->num_children == count_children (nenofex, parent));

  if (is_literal_node (parent))
    {
      assert (!parent->child_list.last);
      assert (parent->lit);
      assert (parent->id);
      assert (parent->lit->negated || parent->id > 0);
      assert (!parent->lit->negated || parent->id < 0);
      assert (parent->id == (parent->lit->negated ?
                             -parent->lit->var->id : parent->lit->var->id));
      assert (parent->lit->occ_list.first);
      assert (parent->lit->occ_list.last);
      assert (parent->occ_link.next || parent->lit->occ_list.last == parent);
      assert (parent->occ_link.prev || parent->lit->occ_list.first == parent);
    }
  else
    {
      assert (parent->id > 0);
      assert (!parent->lit);
      assert ((unsigned int) parent->id > nenofex->num_orig_vars);
      assert (is_or_node (parent) || is_and_node (parent));
      if (!parent->child_list.first)
        {
          assert (!parent->child_list.last);
          assert (0);
        }
      else
        {
          assert (parent->child_list.last);
          Node *ch, *prev;
          prev = 0;
          for (ch = parent->child_list.first; ch; ch = ch->level_link.next)
            {
              assert (!prev || ch->level_link.prev == prev);
              assert (!prev || prev->level_link.next == ch);
              assert (ch->parent->type != ch->type);
              assert (ch->parent == parent);
              assert (ch->level == parent->level + 1);
              assert (ch != parent->child_list.first || !ch->level_link.prev);
              assert (ch != parent->child_list.last || !ch->level_link.next);
              prev = ch;
            }
        }
    }                           /* end: has children */
}


/*
- for assertion checking only
*/
static void
assert_expand_graph_integrity (Nenofex * nenofex)
{
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert_expand_node_integrity (nenofex, cur);

      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            {
              push_stack (stack, child);
            }
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);
}
#endif /* end ifndef NDEBUG */


static void
propagate_truth (Nenofex * nenofex, Node * occ)
{
  assert (occ);
  assert (is_literal_node (occ));

  Node *nenofex_graph_root = nenofex->graph_root;

  if (occ == nenofex_graph_root)
    {
      nenofex->result = NENOFEX_RESULT_SAT;
      remove_and_free_subformula (nenofex, occ);
      return;
    }

  Node *occ_parent = occ->parent;

  if (is_or_node (occ_parent))
    {
      /* BUG-FIX */
      if (occ_parent == nenofex_graph_root)
        nenofex->result = NENOFEX_RESULT_SAT;

      remove_and_free_subformula (nenofex, occ_parent);

      if (!nenofex->graph_root)
        {                       /* graph-root might have been deleted during merging / 1-l-simp */
          /* BUG FIX  --nenofex->result = NENOFEX_RESULT_SAT; */
          assert (nenofex->result != NENOFEX_RESULT_UNKNOWN);
        }
    }
  else                          /* parent is AND */
    {
      remove_and_free_subformula (nenofex, occ);
    }
}


static void
propagate_falsity (Nenofex * nenofex, Node * occ)
{
  assert (occ);
  assert (is_literal_node (occ));

  Node *nenofex_graph_root = nenofex->graph_root;

  if (occ == nenofex_graph_root)
    {
      nenofex->result = NENOFEX_RESULT_UNSAT;
      remove_and_free_subformula (nenofex, occ);
      return;
    }

  Node *occ_parent = occ->parent;

  if (is_and_node (occ_parent))
    {
      /* BUG-FIX */
      if (occ_parent == nenofex_graph_root)
        nenofex->result = NENOFEX_RESULT_UNSAT;

      remove_and_free_subformula (nenofex, occ_parent);

      if (!nenofex->graph_root)
        {                       /* graph-root might have been deleted during merging / 1-l-simp */
          /* BUG FIX  --nenofex->result = NENOFEX_RESULT_UNSAT; */
          assert (nenofex->result != NENOFEX_RESULT_UNKNOWN);
        }
    }
  else                          /* parent is OR */
    {
      remove_and_free_subformula (nenofex, occ);
    }
}


/*
- propagate variable assignment
- NOTE: could eliminate check for truth mark since all new 
    occurrences are appended to occ-list during expansion 
*/
static void
propagate_literals (Nenofex * nenofex, Var * var)
{
  Node *occ;
  Lit *lit;

  lit = var->lits;
  assert (lit->negated);
  while ((occ = lit->occ_list.first))
    {                           /* negative occurrences */
      assert (is_literal_node (occ));

      if (truth_propagation_marked (occ))
        propagate_falsity (nenofex, occ);
      else
        propagate_truth (nenofex, occ);
    }

  lit = var->lits + 1;
  assert (!lit->negated);
  while ((occ = lit->occ_list.first))
    {                           /* positive occurrences */
      assert (is_literal_node (occ));

      if (truth_propagation_marked (occ))
        propagate_truth (nenofex, occ);
      else
        propagate_falsity (nenofex, occ);
    }
}


#ifndef NDEBUG
/*
- for assertion checking only
- returns number of nodes in subformula rooted at 'root'
*/
unsigned int
subformula_size (Nenofex * nenofex, Node * root)
{
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  unsigned int result = 0;

  push_stack (stack, root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      result++;
      if (!is_literal_node (cur))
        {
          Node *ch;
          for (ch = cur->child_list.last; ch; ch = ch->level_link.prev)
            {
              push_stack (stack, ch);
            }
        }
    }                           /* end: while */
  delete_stack (stack);
  return result;
}
#endif /* end ifndef NDEBUG */


static Scope *
find_innermost_non_empty_scope (Nenofex * nenofex)
{
  Scope **scope;
  for (scope = (Scope **) nenofex->scopes->top - 1;
       scope >= (Scope **) nenofex->scopes->elems && (*scope)->is_empty;
       scope--)
    ;

  assert (scope >= (Scope **) nenofex->scopes->elems);

  return *scope;
}


/*
- TODO: incremental version
*/
static int
is_empty_scope (Nenofex * nenofex, Scope * scope)
{
  Var **var, **start;
  start = (Var **) scope->vars->elems;

  for (var = (Var **) scope->vars->top - 1;
       var >= start && ((!variable_has_occs (*var)) || (*var)->eliminated);
       var--)
    ;

#if 1
  assert (nenofex->result != NENOFEX_RESULT_UNKNOWN ||
          !scope->is_empty || var == (Var **) scope->vars->elems - 1);
  assert (nenofex->result != NENOFEX_RESULT_UNKNOWN ||
          var != (Var **) scope->vars->elems - 1 || scope->is_empty);
#endif

  assert (nenofex->result != NENOFEX_RESULT_UNKNOWN ||
          var != (Var **) scope->vars->elems - 1
          || scope->remaining_var_cnt == 0);
  assert (nenofex->result != NENOFEX_RESULT_UNKNOWN
          || scope->remaining_var_cnt != 0
          || var == (Var **) scope->vars->elems - 1);

  return (var == (Var **) scope->vars->elems - 1);
}


/*
- mark variables for cost update
- from root of subformula where graph was modified, up to graph root
- check if encountered nodes are LCA of a variable
- TODO: clarify failed assertions
*/
void
mark_affected_scope_variables_for_cost_update (Nenofex * nenofex,
                                               Node * exp_root)
{
  assert (exp_root);

  Node *prev = exp_root;
  Node *cur = exp_root->parent;

  /* if cur has no parent at beginning (i.e. is at root), 
     then if there is a var that has lca == cur, 
     it is already marked if it has occs in the current subformula
   */

  while (cur)
    {
      Stack *lca_child_list_occs = prev->lca_child_list_occs;

      /* FAILED: possibly same reason as in ATPG    
         --assert(!lca_child_list_occs || !count_stack(lca_child_list_occs) || 
         cur->var_lca_list.first); */

      if (lca_child_list_occs)
        {
          /* NOT SURE */
          /*
             assert(!count_stack(lca_child_list_occs) || cur->var_lca_list.first);
             assert(!count_stack(lca_child_list_occs) || cur->var_lca_list.last);
           */

          void **v_var, **end;
          end = lca_child_list_occs->top;

          for (v_var = lca_child_list_occs->elems; v_var < end; v_var++)
            {
              Var *var = *v_var;
              inc_score_update_mark (var);
              collect_variable_for_update (nenofex, var);
            }                   /* end: for all variables where parent is lca */

        }                       /* end: cur is lca of at least one variable */

      assert (!is_literal_node (prev) || !prev->child_list.first);

      /* search for lits at path node -> vars have to be marked for dec-score update */
      Node *ch;
      for (ch = prev->child_list.first;
           ch && is_literal_node (ch); ch = ch->level_link.next)
        {
          Var *var = ch->lit->var;
          if (var->exp_costs.lca_object.lca)    /* BUG FIX */
            {
              dec_score_update_mark (var);
              collect_variable_for_update (nenofex, var);
            }
        }                       /* end: for all literals */

      prev = cur;
      cur = cur->parent;
    }                           /* end: while root of graph not checked */

  /* search for lits at path node -> vars have to be marked for dec-score update */
  Node *ch;
  for (ch = prev->child_list.first; ch && is_literal_node (ch);
       ch = ch->level_link.next)
    {
      Var *var = ch->lit->var;
      if (var->exp_costs.lca_object.lca)        /* BUG FIX */
        {
          dec_score_update_mark (var);
          collect_variable_for_update (nenofex, var);
        }
    }                           /* end: for all literals */
}


/*
- could be done incrementally
- checks whether lca(var) has occurrence of 'var' as child
- NOTE: must handle situation for non-innermost universal vars
*/
static Node *
lca_children_contain_lit (Var * var, LCAObject * lca_object)
{
  assert (lca_object->children);
  assert (lca_object->lca);

  Node **ch, *child;
  for (ch = lca_object->children; (child = *ch); ch++)
    {
      if (is_literal_node (child) && child->lit->var == var)
        break;
    }

  return child;
}


/*
- traverse subformula and mark encountered variables
- needed during a special case of variable-expansion
*/
static void
mark_occs_propagation_var_cost_update (Nenofex * nenofex, Var * var,
                                       const int mark_var_truth)
{
  assert (mark_var_truth == 0 || mark_var_truth == 1);
  assert (var->exp_costs.lca_object.lca);

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, var->exp_costs.lca_object.lca);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (is_literal_node (cur))
        {
          Var *cur_var = cur->lit->var;

          if (cur_var == var && mark_var_truth)
            truth_propagation_mark (cur);
          else if (cur_var != var)
            {
              lca_update_mark (cur_var);
              inc_score_update_mark (cur_var);
              dec_score_update_mark (cur_var);
              collect_variable_for_update (nenofex, cur_var);
            }
        }
      else
        {
          Node *ch;
          for (ch = cur->child_list.last; ch; ch = ch->level_link.prev)
            push_stack (stack, ch);
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);
}


/* FUNCTIONS FOR MAINTAINING 'CHANGED-SUBFORMULA'
- changed-subformula: the region of the graph where changes occurred 
- represented by an LCAObject (like LCA of variable)
*/

/*
- nodes which are children of LCA of 'changed-subformula' 
    have pointer to position in child-list
- assign position for all node
*/
static void
set_pos_in_changed_child_list (Nenofex * nenofex)
{
  Node **ch, *child;
  for (ch = nenofex->changed_subformula.children; (child = *ch); ch++)
    {
      child->changed_ch_list_pos = ch;
    }
}


#ifndef NDEBUG
/*
- for assertion checking only
*/
static void
assert_pos_in_changed_ch_list (Nenofex * nenofex)
{
  Node **ch, *child;
  for (ch = nenofex->changed_subformula.children; (child = *ch); ch++)
    {
      assert (child->changed_ch_list_pos == ch);
    }
}
#endif /* end ifndef NDEBUG */


/*
- given LCAObject represents 'changed-subformula'
*/
static void
assign_changed_subformula (Nenofex * nenofex, LCAObject * lca_object)
{
  LCAObject *changed_subformula = &nenofex->changed_subformula;

  assert (!changed_subformula->lca);
  assert (changed_subformula->children);
  assert (!is_literal_node (lca_object->lca));
  assert (lca_object->num_children >= 2);

  reset_changed_lca_object (nenofex);

  assert (changed_subformula->top_p == changed_subformula->children);
  assert (changed_subformula->num_children == 0);
  assert (*(changed_subformula->children) == 0);

  int realloc_called =
    (changed_subformula->size_children <= lca_object->num_children) ? 1 : 0;

#ifndef NDEBUG
  unsigned int old_size = changed_subformula->size_children;
#endif

  Node **ch, *child;

  if (realloc_called)
    {
      for (ch = lca_object->children; (child = *ch); ch++)
        {                       /* add nodes */
          add_lca_child (changed_subformula, child);
        }                       /* end: for */

      set_pos_in_changed_child_list (nenofex);
    }
  else                          /* no realloc of memory will occur -> may set pointer to pos during copying */
    {
      Node **pos = changed_subformula->top_p;

      for (ch = lca_object->children; (child = *ch); ch++)
        {                       /* add nodes and set position */
          child->changed_ch_list_pos = pos++;
          add_lca_child (changed_subformula, child);
        }                       /* end: for */

      assert (pos ==
              changed_subformula->children +
              changed_subformula->num_children);
    }

#ifndef NDEBUG
  assert (!realloc_called || old_size != changed_subformula->size_children);
  assert (realloc_called || old_size == changed_subformula->size_children);
#endif

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif

  assert (changed_subformula->num_children >= 2);
  assert (changed_subformula->top_p ==
          changed_subformula->children + changed_subformula->num_children);

  changed_subformula->lca = lca_object->lca;

  assert (!is_literal_node (nenofex->changed_subformula.lca));
}


static void
unify_changed_lca_children (Nenofex * nenofex, LCAObject * lca_object)
{
  LCAObject *changed_subformula = &(nenofex->changed_subformula);

  int realloc_possible =
    ((changed_subformula->size_children - changed_subformula->num_children) <=
     lca_object->num_children) ? 1 : 0;

  Node **ch, *child;
  if (realloc_possible)
    {
      for (ch = lca_object->children; (child = *ch); ch++)
        {                       /* add children if not already contained */
          if (!child->changed_ch_list_pos)
            add_lca_child (changed_subformula, child);
        }                       /* end: for */

      set_pos_in_changed_child_list (nenofex);
    }
  else
    {
      Node **pos = changed_subformula->top_p;

      for (ch = lca_object->children; (child = *ch); ch++)
        {                       /* add children if not already contained */
          if (!child->changed_ch_list_pos)
            {
              child->changed_ch_list_pos = pos++;
              add_lca_child (changed_subformula, child);
            }
        }                       /* end: for */
    }

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
}


static void
remove_changed_lca_child (Nenofex * nenofex, Node * node)
{
  Node **pos = node->changed_ch_list_pos;
  LCAObject *changed_subformula = &(nenofex->changed_subformula);

  assert (pos);

  assert (changed_subformula->num_children <
          changed_subformula->size_children);
  assert (changed_subformula->top_p > changed_subformula->children);

#ifndef NDEBUG
  if (nenofex->cur_expanded_var)
    {
      /* need a kind of workaround in following assertions; during expansion we 
         might have to remove nodes from changed-child-list */
      assert (changed_subformula->lca ==
              nenofex->cur_expanded_var->exp_costs.lca_object.lca
              || changed_subformula->num_children >= 2);
      assert (changed_subformula->lca ==
              nenofex->cur_expanded_var->exp_costs.lca_object.lca
              || changed_subformula->top_p >=
              changed_subformula->children + 2);
    }
  else
    {
      assert (changed_subformula->num_children >= 2);
      assert (changed_subformula->top_p >= changed_subformula->children + 2);
    }
#endif

  assert (*changed_subformula->top_p == 0);
  assert (changed_subformula->num_children ==
          (unsigned int) (changed_subformula->top_p -
                          changed_subformula->children));

  changed_subformula->num_children--;
  changed_subformula->top_p--;  /* last element overwrites the one to be deleted */

  (*pos)->changed_ch_list_pos = 0;      /* reset pointer of node to be removed */

  if (pos != changed_subformula->top_p)
    {                           /* average case; else, simply cut off last element */
      *pos = *changed_subformula->top_p;
      (*pos)->changed_ch_list_pos = pos;
    }

  *changed_subformula->top_p = 0;

  assert (changed_subformula->top_p >= changed_subformula->children);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
}


static void
replace_changed_lca_child (Nenofex * nenofex, Node * old_node,
                           Node * new_node)
{
  Node **old_pos = old_node->changed_ch_list_pos;
  LCAObject *changed_subformula = &(nenofex->changed_subformula);

  assert (!new_node->changed_ch_list_pos);
  assert (old_pos);

  assert (changed_subformula->num_children <
          changed_subformula->size_children);
  assert (changed_subformula->num_children >= 2);
  assert (changed_subformula->top_p >= changed_subformula->children + 2);
  assert (*changed_subformula->top_p == 0);
  assert (changed_subformula->num_children ==
          (unsigned int) (changed_subformula->top_p -
                          changed_subformula->children));

  old_node->changed_ch_list_pos = 0;    /* reset pointer of old node */

  *old_pos = new_node;
  new_node->changed_ch_list_pos = old_pos;

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif

  assert (changed_subformula->num_children >= 2);
  assert (changed_subformula->top_p ==
          changed_subformula->children + changed_subformula->num_children);
}


void
add_changed_lca_child (Nenofex * nenofex, Node * node)
{
  LCAObject *changed_subformula = &(nenofex->changed_subformula);
  int realloc_called =
    changed_subformula->num_children ==
    changed_subformula->size_children - 1 ? 1 : 0;

  assert (!node->changed_ch_list_pos);

#ifndef NDEBUG
  unsigned int old_size = changed_subformula->size_children;
#endif

  add_lca_child (changed_subformula, node);

  if (realloc_called)
    set_pos_in_changed_child_list (nenofex);
  else
    node->changed_ch_list_pos = changed_subformula->top_p - 1;

  assert (node->changed_ch_list_pos == changed_subformula->top_p - 1);

#ifndef NDEBUG
  assert (!realloc_called || old_size != changed_subformula->size_children);
#endif

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
}


void
reset_changed_lca_object (Nenofex * nenofex)
{
  LCAObject *lca_object = &(nenofex->changed_subformula);

  Node **ch, *child;
  for (ch = lca_object->children; (child = *ch); ch++)
    {
      assert (child->changed_ch_list_pos == ch);

      child->changed_ch_list_pos = 0;
      *ch = 0;
    }

  lca_object->num_children = 0;
  lca_object->top_p = lca_object->children;
  lca_object->lca = 0;
}


static void
update_changed_subformula (Nenofex * nenofex, LCAObject * lca_object)
{
  LCAObject *changed_subformula = &(nenofex->changed_subformula);

  Node *changed_lca_cur = changed_subformula->lca;
  Node *changed_lca_new = lca_object->lca;

  assert (changed_lca_cur);
  assert (changed_lca_new);

  if (changed_lca_cur == changed_lca_new)
    {
      unify_changed_lca_children (nenofex, lca_object);
      return;
    }

  Node *high_node, *low_node, *high_node_prev, *low_node_prev;

  low_node_prev = 0;

  if (changed_lca_cur->level >= changed_lca_new->level)
    {
      high_node = changed_lca_new;
      low_node = changed_lca_cur;
    }
  else
    {
      high_node = changed_lca_cur;
      low_node = changed_lca_new;
    }

  unsigned const int high_node_level = high_node->level;

  while (high_node_level < low_node->level)
    {                           /* level-balancing -> check if high_node is reachable 
                                   from low_nide via parent-pointers */
      low_node_prev = low_node;
      low_node = low_node->parent;
    }

  if (high_node == low_node)
    {                           /* check whether low_node is successor of an lca-child of 
                                   high node or only of an "ordinary" child */

      if (high_node == changed_lca_cur)
        {
          if (low_node_prev->changed_ch_list_pos)
            {                   /* nothing to be done: 'changed_lca_new' contained in 'changed_lca_cur' */
              ;
            }
          else
            {                   /* add child to changed_lca-children */
              add_changed_lca_child (nenofex, low_node_prev);
            }
        }
      else                      /* high_node == changed_lca_new */
        {                       /* set 'changed_lca := changed_lca_new' */
          assert (high_node == changed_lca_new);

          changed_subformula->lca = 0;
          assign_changed_subformula (nenofex, lca_object);

          if (!low_node_prev->changed_ch_list_pos)
            add_changed_lca_child (nenofex, low_node_prev);

        }                       /* end: high_node == changed_lca_new */

    }
  else
    {                           /* need to find 'lca(changed_cur, changed_new)' by 
                                   following parent-pointers in parallel */

      do
        {
          low_node_prev = low_node;
          low_node = low_node->parent;

          high_node_prev = high_node;
          high_node = high_node->parent;

          assert (high_node);
          assert (low_node);
        }
      while (high_node != low_node);
      assert (high_node);

      reset_changed_lca_object (nenofex);

      /* must have had at least 2 children, hence capacity greater than 2 */
      assert (changed_subformula->size_children > 2);

#ifndef NDEBUG
      unsigned int old_size = changed_subformula->size_children;
#endif

      changed_subformula->lca = high_node;

      low_node_prev->changed_ch_list_pos = changed_subformula->top_p;
      add_lca_child (changed_subformula, low_node_prev);

      high_node_prev->changed_ch_list_pos = changed_subformula->top_p;
      add_lca_child (changed_subformula, high_node_prev);

#ifndef NDEBUG
      /* 'realloc' must not have been called */
      assert (old_size == changed_subformula->size_children);
#endif
    }                           /* end: compute 'lca(changed_cur, changed_new)' */

  assert (changed_subformula->num_children <
          changed_subformula->size_children);

  assert (changed_subformula->num_children >= 2);
  assert (changed_subformula->top_p ==
          changed_subformula->children + changed_subformula->num_children);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif

  assert (!is_literal_node (nenofex->changed_subformula.lca));
}


#define assign_or_update_changed_subformula(lca_object_p) \
  if (!nenofex->changed_subformula.lca && !is_literal_node((lca_object_p)->lca)) \
    assign_changed_subformula(nenofex, (lca_object_p)); \
  else if (nenofex->changed_subformula.lca) \
    update_changed_subformula(nenofex, (lca_object_p)); \


#ifndef NDEBUG
static void assert_all_subformula_sizes (Nenofex * nenofex);
#endif


/* ---------- START: POST-EXPANSION FLATTENING ---------- */

#define DISTRIBUTIVITY_VAR_POS_MARK 1
#define DISTRIBUTIVITY_VAR_NEG_MARK 2
#define distributivity_var_marked(var) ((var)->simp_mark)
#define distributivity_var_unmark(var) ((var)->simp_mark = 0)
#define distributivity_var_pos_marked(var) ((var)->simp_mark == SIMPLIFY_VAR_POS_MARK)
#define distributivity_var_neg_marked(var) ((var)->simp_mark == SIMPLIFY_VAR_NEG_MARK)
#define distributivity_var_pos_mark(var) ((var)->simp_mark = SIMPLIFY_VAR_POS_MARK)
#define distributivity_var_neg_mark(var) ((var)->simp_mark = SIMPLIFY_VAR_NEG_MARK)

/*
- like resolution (but resolution variable which connects clauses is already gone)
- multiply out clauses
- trivial clauses and double literals will be discarded
- ACTUALLY: not only clauses but graphs are possible as well
*/
Node *
apply_distributivity_of_or_over_and (Nenofex * nenofex, Node * node1,
                                     Node * node2)
{
  assert (!nenofex->distributivity_deleting_redundancies);
  /* flag is used during deletion in order to indicate that 'loose' nodes are deleted */
  nenofex->distributivity_deleting_redundancies = 1;

  Node *result_or;
  Node *copy;

  if (is_literal_node (node1) && is_literal_node (node2))
    {                           /* special case */
      Lit *lit1 = node1->lit;
      Lit *lit2 = node2->lit;
      assert (!distributivity_var_marked (lit1->var));
      assert (!distributivity_var_marked (lit2->var));

      if (lit1->var == lit2->var)
        {                       /* avoid adding redundant nodes */
          if ((lit1->negated && !lit2->negated) ||
              (lit2->negated && !lit1->negated))
            {                   /* will get trivial clause */
              result_or = 0;
            }
          else                  /* will get double literals */
            {
              result_or = copy_formula (nenofex, node1);
            }
        }
      else                      /* different variables -> add both to new OR */
        {
          result_or = or_node (nenofex);
          result_or->size_subformula = 1;

          copy = copy_formula (nenofex, node1);
          add_node_to_child_list (nenofex, result_or, copy);
          result_or->size_subformula += copy->size_subformula;

          copy = copy_formula (nenofex, node2);
          add_node_to_child_list (nenofex, result_or, copy);
          result_or->size_subformula += copy->size_subformula;
        }
    }                           /* end: both are literals */
  else                          /* at least one OR */
    {
      result_or = or_node (nenofex);
      result_or->size_subformula = 1;

      if (is_literal_node (node2))
        {
          Node *tmp = node1;
          node1 = node2;
          node2 = tmp;
        }
      assert (is_or_node (node2));

      if (is_literal_node (node1))
        {
          copy = copy_formula (nenofex, node1);
          add_node_to_child_list (nenofex, result_or, copy);
          result_or->size_subformula += copy->size_subformula;

          Lit *lit = node1->lit;
          assert (!distributivity_var_marked (lit->var));
          if (!lit->negated)
            distributivity_var_pos_mark (lit->var);
          else
            distributivity_var_neg_mark (lit->var);
        }
      else                      /* both node1 and node2 are or */
        {
          assert (is_or_node (node1));
          Node *child1;
          for (child1 = node1->child_list.first;
               child1; child1 = child1->level_link.next)
            {
              assert (is_literal_node (child1) || is_and_node (child1));
              copy = copy_formula (nenofex, child1);
              add_node_to_child_list (nenofex, result_or, copy);
              result_or->size_subformula += copy->size_subformula;

              if (is_literal_node (child1))
                {
                  Lit *lit = child1->lit;
                  assert (!distributivity_var_marked (lit->var));
                  if (!lit->negated)
                    distributivity_var_pos_mark (lit->var);
                  else
                    distributivity_var_neg_mark (lit->var);
                }
            }                   /* end: for all children of node1 */
        }                       /* end: both node1 and node2 are or */

      /* add children of node2 to result_or; ignore double literals, 
         abort if trivial clause is generated */
      Node *child2;
      for (child2 = node2->child_list.first; child2;
           child2 = child2->level_link.next)
        {                       /* check for redundancies before adding */
          if (is_literal_node (child2))
            {
              Lit *lit = child2->lit;
              Var *var = lit->var;
              if (distributivity_var_neg_marked (var))
                {
                  if (!lit->negated)
                    {           /* trivial clause -> abort */
                      break;
                    }
                }
              else if (distributivity_var_pos_marked (var))
                {
                  if (lit->negated)
                    {           /* trivial clause -> abort */
                      break;
                    }
                }
              else              /* new variable -> add and mark */
                {
                  copy = copy_formula (nenofex, child2);
                  add_node_to_child_list (nenofex, result_or, copy);
                  result_or->size_subformula += copy->size_subformula;

                  assert (!distributivity_var_marked (var));
                  if (!lit->negated)
                    distributivity_var_pos_mark (var);
                  else
                    distributivity_var_neg_mark (var);
                }               /* end: add new literal */
            }
          else                  /* is and */
            {
              assert (is_and_node (child2));
              copy = copy_formula (nenofex, child2);
              add_node_to_child_list (nenofex, result_or, copy);
              result_or->size_subformula += copy->size_subformula;
            }                   /* end: child2 is not a literal */
        }                       /* end: for all children of node2 */

      assert (result_or->num_children != 0);
      assert (result_or->child_list.first);
      assert (result_or->child_list.last);

      /* unmark all variables */
      Node *clean_ch;
      for (clean_ch = result_or->child_list.first;
           clean_ch && is_literal_node (clean_ch);
           clean_ch = clean_ch->level_link.next)
        {
          assert (distributivity_var_marked (clean_ch->lit->var));
          distributivity_var_unmark (clean_ch->lit->var);
        }

      if (result_or->num_children == 1)
        {                       /* special case */
          if (child2)
            {                   /* trivial clause: loop exited early -> clean up */
              Node *single_child = result_or->child_list.first;
              unlink_node (nenofex, result_or->child_list.first);
              assert (!single_child->parent);
              assert (is_literal_node (single_child));
              delete_node (nenofex, result_or);
              result_or = 0;
              /* will unlink literal from occ-list as well */
              remove_and_free_subformula (nenofex, single_child);
            }
          else                  /* return single child as result */
            {                   /* CONJECTURE: this case should never occur */
              Node *single_child = result_or->child_list.first;
              unlink_node (nenofex, result_or->child_list.first);
              assert (is_literal_node (single_child));
              delete_node (nenofex, result_or);
              result_or = single_child;
            }
        }
      else                      /* more than one child -> check if trivial or not */
        {
          if (child2)
            {                   /* trivial clause: loop exited early -> clean up */
              assert (!result_or->parent);
              /* will unlink literals from occ-list as well */
              remove_and_free_subformula (nenofex, result_or);
              result_or = 0;
            }
        }

    }                           /* end: not both are literals */

  nenofex->distributivity_deleting_redundancies = 0;
  return result_or;
}


/* 
- case: 'split-or' has two ANDs 
*/
void
flatten_by_ands_at_split_or (Nenofex * nenofex,
                             LCAObject * changed_subformula_new)
{
  Node *split_or = nenofex->existential_split_or;
  Node *split_or_parent = split_or->parent;

  assert (split_or->parent);
  assert (split_or->num_children == 2);
  assert (is_and_node (split_or->child_list.last));
  assert (is_and_node (split_or->child_list.first));
  assert (split_or->child_list.first->level_link.next ==
          split_or->child_list.last);

  int total_size_added = 0;
  Node *disjunction;            /* partial result when distributing OR over AND */
  Node *first_and_child2 = split_or->child_list.last->child_list.first;

  Node *and_child1, *and_child2;
  for (and_child1 = split_or->child_list.first->child_list.first;
       and_child1; and_child1 = and_child1->level_link.next)
    {
      assert (is_literal_node (and_child1) || is_or_node (and_child1));
      for (and_child2 = first_and_child2;
           and_child2; and_child2 = and_child2->level_link.next)
        {
          assert (is_literal_node (and_child2) || is_or_node (and_child2));
          disjunction =
            apply_distributivity_of_or_over_and (nenofex, and_child1,
                                                 and_child2);

          if (disjunction)
            {
              total_size_added += disjunction->size_subformula;
              add_node_to_child_list (nenofex, split_or_parent, disjunction);
              add_lca_child (changed_subformula_new, disjunction);
              update_level (nenofex, disjunction);
            }
        }                       /* end: for all children at second AND */
    }                           /* end: for all children at first AND */

  update_size_subformula (nenofex, split_or_parent, total_size_added);
  assert (!nenofex->distributivity_deleting_redundancies);
  remove_and_free_subformula (nenofex, split_or);
}


/* 
- case: 'split-or' has one AND and at least one literal 
- if exactly one literal 'lit' then 'scratch_node' == 'lit'
- else 'scratch_node' is an new (scratch-) OR-node which contains copies of lits
*/
void
flatten_by_literals_at_split_or (Nenofex * nenofex, Node * scratch_node,
                                 LCAObject * changed_subformula_new)
{
  Node *split_or = nenofex->existential_split_or;
  Node *split_or_parent = split_or->parent;

  assert (split_or->parent);
  assert (is_and_node (split_or->child_list.last));
  assert (!is_and_node (split_or->child_list.first));

  int total_size_added = 0;

  Node *disjunction;            /* partial result when distributing OR over AND */

  Node *and_child;
  for (and_child = split_or->child_list.last->child_list.first;
       and_child; and_child = and_child->level_link.next)
    {
      assert (is_literal_node (and_child) || is_or_node (and_child));
      disjunction =
        apply_distributivity_of_or_over_and (nenofex, scratch_node,
                                             and_child);

      if (disjunction)
        {
          total_size_added += disjunction->size_subformula;
          add_node_to_child_list (nenofex, split_or_parent, disjunction);
          add_lca_child (changed_subformula_new, disjunction);
          update_level (nenofex, disjunction);
        }
    }                           /* end: for all children at AND */

  update_size_subformula (nenofex, split_or_parent, total_size_added);
  assert (!nenofex->distributivity_deleting_redundancies);
  remove_and_free_subformula (nenofex, split_or);
}


void
post_expansion_flattening (Nenofex * nenofex,
                           LCAObject * changed_subformula_new)
{
  if (nenofex->options.show_progress_specified)
    fprintf (stderr, "post-expansion flattening\n");

  Node *split_or = nenofex->existential_split_or;
  Node *split_or_first_child = 0;

  if (is_and_node (split_or->child_list.last))
    {
      if (is_and_node ((split_or_first_child = split_or->child_list.first)))
        {
          assert (split_or->num_children == 2);
          nenofex->cnt_post_expansion_flattenings++;
          flatten_by_ands_at_split_or (nenofex, changed_subformula_new);
        }
      else
        {
          /* first is lit-node -> possibly one or more literals 
             at split-or -> combine them in one OR */
          assert (is_literal_node (split_or_first_child));
          Node *scratch = 0;

          if (is_literal_node (split_or_first_child->level_link.next))
            {
              scratch = or_node (nenofex);

              Node *child;
              for (child = split_or_first_child;
                   child && is_literal_node (child);
                   child = child->level_link.next)
                {
                  Node *lit_copy = copy_formula (nenofex, child);
                  add_node_to_child_list (nenofex, scratch, lit_copy);
                }               /* end: for all children */
            }                   /* end: more than one literal */
          else
            {                   /* only one literal at split-or */
              assert (is_and_node (split_or_first_child->level_link.next));
              assert (split_or_first_child->level_link.next ==
                      split_or->child_list.last);
              scratch = split_or_first_child;
            }

          nenofex->cnt_post_expansion_flattenings++;
          flatten_by_literals_at_split_or (nenofex, scratch,
                                           changed_subformula_new);

          if (is_or_node (scratch))
            {
              assert (!nenofex->distributivity_deleting_redundancies);
              nenofex->distributivity_deleting_redundancies = 1;        /* workaround */
              assert (!scratch->parent);
              remove_and_free_subformula (nenofex, scratch);
              nenofex->distributivity_deleting_redundancies = 0;
            }
        }                       /* end: literals at split-or */
    }
#ifndef NDEBUG
  else
    {
      Node *child;
      for (child = split_or->child_list.first;
           child; child = child->level_link.next)
        {
          assert (is_literal_node (child));
        }
    }
#endif /* end: ifndef NDEBUG */

#ifndef NDEBUG
#if ASSERT_POST_EXPANSION_FLATTENING
  assert_all_child_occ_lists_integrity (nenofex);
  assert_all_occ_lists_integrity (nenofex);
  assert_all_subformula_sizes (nenofex);
#endif
#endif
/* TODO: LCA-CH-LIST-OCCS (?) */
}


/*
- called before post-expansion flattening is carried out
- check whether situation at 'split-or' stems from an expansion on a CNF-like graph
- if not then do not flatten
- ONE REASON IS: it may happen that 'split-or' has more than 2 AND-children 
    after expanded variable has been assigned and propagated
    but flattening works only if there are exactly 2 AND-children or 
    1 AND-child and arbitrarily many literal-children
*/
int
post_expansion_flattening_check_for_cnf_form (Node * split_or)
{
  int result = 1;
  const unsigned int level_threshold = split_or->level + 3;

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, split_or);

  Node *cur;
  while ((cur = pop_stack (stack)) && result)
    {
      if (!is_literal_node (cur))
        {
          Node *child;
          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              push_stack (stack, child);
            }
        }
      else
        {
          /* is literal -> check distance to split-or: 
             may be at most 3 else graph has not CNF-like form */
          if (cur->level > level_threshold)
            result = 0;
        }
    }

  delete_stack (stack);

  return result;
}

void
post_expansion_flattening_reset_changed_new (LCAObject *
                                             changed_subformula_new)
{
  /* workaround: manually reset 'changed_subformula_new' */
  Node **ch, *child;
  for (ch = changed_subformula_new->children; (child = *ch); ch++)
    {
      *ch = 0;
    }
  changed_subformula_new->num_children = 0;
  changed_subformula_new->top_p = changed_subformula_new->children;
  changed_subformula_new->lca = 0;
}

/* ---------- END: POST-EXPANSION FLATTENING ---------- */


/*
- core function: expand an existential variable
- depends on LCA of expanded variable and  LCA-children, a subgraph is copied
- expanded variable will be set to false/true in original/copied subgraph
- graph properties need to be maintained during copying (size, level)
- need to maintain 'changed-subformula'
- NON-INCREASING EXPANSION: possible if LCA(var) has an occurrence of 'var' as 
    child (need not copy, can propagate immediately)
*/
static void
expand_existential_variable (Nenofex * nenofex, Var * var)
{
  assert (var);
  assert (is_existential_scope (var->scope));
  assert (!nenofex->cur_expanded_var);
  assert (!nenofex->existential_split_or);
  assert (var->scope == *nenofex->cur_scope
          || var->scope == *nenofex->next_scope);
  assert (variable_has_occs (var));

  nenofex->cur_expanded_var = var;
  LCAObject *lca_object = &var->exp_costs.lca_object;
  Node *lca_object_lca = lca_object->lca;

  LCAObject changed_subformula_new;
  init_lca_object (&changed_subformula_new);

  assert (lca_object_lca);
  assert (lca_object->num_children != 0 || is_literal_node (lca_object_lca));
  assert (!is_literal_node (lca_object_lca) || lca_object->num_children == 0);

  /* flags for revised expansion */
  Node *contained_lit = lca_children_contain_lit (var, lca_object);
  assert (!contained_lit || contained_lit->lit->var == var);
  Node *preemptive_occ = 0;

#if COMPUTE_NUM_NON_INC_EXPANSIONS
  if (contained_lit)
    nenofex->stats.num_non_inc_expansions++;
#endif

  /* used in for post-expansion flattening */
  int set_existential_split_or = 0;
  if (nenofex->options.post_expansion_flattening_specified && ((var->lits[0].
                                                                occ_cnt == 1)
                                                               || (var->
                                                                   lits[1].
                                                                   occ_cnt ==
                                                                   1)
                                                               || (var->
                                                                   lits[0].
                                                                   occ_cnt ==
                                                                   2
                                                                   && var->
                                                                   lits[1].
                                                                   occ_cnt ==
                                                                   2) || (0
                                                                          &&
                                                                          var->
                                                                          lits
                                                                          [0].
                                                                          occ_cnt
                                                                          *
                                                                          var->
                                                                          lits
                                                                          [1].
                                                                          occ_cnt
                                                                          <
                                                                          25)))
    {
      set_existential_split_or = 1;
    }

#ifndef NDEBUG
#if ASSERT_SIZES_IN_EXPANSION
  unsigned int size_before_exp =
    subformula_size (nenofex, nenofex->graph_root);
  unsigned int inc_score = var->exp_costs.inc_score;
  unsigned int dec_score = var->exp_costs.dec_score;
  int check_size = size_before_exp + inc_score - dec_score;
  assert (check_size >= 0);
#endif
#endif

  unsigned int num_children = lca_object_lca->num_children;
  NodeType type = lca_object_lca->type;
  int update_size_delta = 0;

  if (num_children == lca_object->num_children)
    {                           /* CASE 1 */
      if (type == NODE_TYPE_OR)
        {                       /* CASE 1.1 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_E_OR_ALL++;
#endif

          if (!contained_lit)
            {
              changed_subformula_new.lca = lca_object_lca;

              /* append copy of children */
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {
                  Node *copy =
                    copy_formula_mark_propagation (nenofex, child, var);

                  add_lca_child (&changed_subformula_new, child);
                  add_lca_child (&changed_subformula_new, copy);

                  add_node_to_child_list (nenofex, lca_object_lca, copy);
                  update_size_delta += copy->size_subformula;

                  assert (!is_literal_node ((child))
                          || (child)->lit->var == var);
                }
              assert (2 * num_children == lca_object_lca->num_children);

              update_size_subformula (nenofex, lca_object_lca,
                                      update_size_delta);

              assign_or_update_changed_subformula (&changed_subformula_new);

            }                   /* end: relevant children do not contain lit */
          else
            {                   /* propagate that literal preemptively -> will delete parent */
              preemptive_occ = contained_lit;

              assign_or_update_changed_subformula (lca_object);
            }
        }
      else if (type == NODE_TYPE_AND)
        {                       /* CASE 1.2 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_E_AND_ALL++;
#endif

          if (contained_lit)
            {
              if (!contained_lit->lit->negated) /* TODO: remove branch, pass flag */
                mark_occs_propagation_var_cost_update (nenofex, var, 1);
              else
                mark_occs_propagation_var_cost_update (nenofex, var, 0);

              assign_or_update_changed_subformula (lca_object);
            }
          else if (lca_object_lca == nenofex->graph_root)
            {
              Node *split_or = or_node (nenofex);

              changed_subformula_new.lca = split_or;

              nenofex->graph_root = split_or;
              add_node_to_child_list (nenofex, split_or, lca_object_lca);
              update_level (nenofex, lca_object_lca);   /* NOTE: merge copy / update-level? */
              Node *copy =
                copy_formula_mark_propagation (nenofex, lca_object_lca, var);
              add_node_to_child_list (nenofex, split_or, copy);

              add_lca_child (&changed_subformula_new, lca_object_lca);
              add_lca_child (&changed_subformula_new, copy);

              nenofex->graph_root->size_subformula =
                1 + 2 * lca_object_lca->size_subformula;

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
          else                  /* average case */
            {
              assert (lca_object_lca->parent);
              assert (is_or_node (lca_object_lca->parent));

              changed_subformula_new.lca = lca_object_lca->parent;

              Node *copy =
                copy_formula_mark_propagation (nenofex, lca_object_lca, var);

              add_lca_child (&changed_subformula_new, lca_object_lca);
              add_lca_child (&changed_subformula_new, copy);

              add_node_to_child_list (nenofex, lca_object_lca->parent, copy);

              update_size_subformula (nenofex, lca_object_lca->parent,
                                      lca_object_lca->size_subformula);

              assert (!is_literal_node (copy));

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
        }
      else                      /* type == NODE_TYPE_LITERAL */
        {                       /* CONJECTURE: should not occur if all unates are fully eliminated */
          assert (!lca_object_lca->occ_link.prev);      /* only 1 occ */
          assert (!lca_object_lca->occ_link.next);

          if (!lca_object_lca->parent)
            {
              if (!lca_object_lca->lit->negated)
                truth_propagation_mark (lca_object_lca);
            }
          else if (is_or_node (lca_object_lca->parent))
            {
              if (!lca_object_lca->lit->negated)
                truth_propagation_mark (lca_object_lca);
            }
          else                  /* AND */
            {
              assert (is_and_node (lca_object_lca->parent));

              if (!lca_object_lca->lit->negated)
                truth_propagation_mark (lca_object_lca);
            }

          assign_or_update_changed_subformula (lca_object);
        }
    }
  else
    {                           /* CASE 2: not all children of LCA need to be copied, only relevant ones */
      if (type == NODE_TYPE_OR)
        {                       /* CASE 2.1 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_E_OR_SUBSET++;
#endif

          if (!contained_lit)
            {
              changed_subformula_new.lca = lca_object_lca;

              /* append copy of children */
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {
                  Node *copy =
                    copy_formula_mark_propagation (nenofex, child, var);
                  add_node_to_child_list (nenofex, lca_object_lca, copy);
                  assert (!is_literal_node ((child))
                          || (child)->lit->var == var);
                  update_size_delta += copy->size_subformula;

                  add_lca_child (&changed_subformula_new, child);
                  add_lca_child (&changed_subformula_new, copy);
                }

              assert (num_children + lca_object->num_children ==
                      lca_object_lca->num_children);

              update_size_subformula (nenofex, lca_object_lca,
                                      update_size_delta);

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
          else
            {
              preemptive_occ = contained_lit;

              assign_or_update_changed_subformula (lca_object);
            }
        }
      else if (type == NODE_TYPE_AND)
        {                       /* CASE 2.2 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_E_AND_SUBSET++;
#endif

          if (contained_lit)
            {
              if (!contained_lit->lit->negated) /* TODO: remove branch, pass flag */
                mark_occs_propagation_var_cost_update (nenofex, var, 1);
              else
                mark_occs_propagation_var_cost_update (nenofex, var, 0);

              assign_or_update_changed_subformula (lca_object);
            }
          else
            {
              Node *split_or = or_node (nenofex);

              if (set_existential_split_or)
                nenofex->existential_split_or = split_or;

              changed_subformula_new.lca = split_or;

              split_or->size_subformula = 1;
              add_node_to_child_list (nenofex, lca_object_lca, split_or);
              assert (split_or->level == lca_object_lca->level + 1);

              Node *new_and = and_node (nenofex);
              new_and->size_subformula = 1;
              add_node_to_child_list (nenofex, split_or, new_and);
              assert (new_and->level == split_or->level + 1);

              /* BUG - FIX */
              LCAObject *changed_subformula = &(nenofex->changed_subformula);
              unsigned int num_changed_ch_before_relinking =
                changed_subformula->num_children;
              assert (!changed_subformula->lca
                      || changed_subformula->num_children >= 2);

              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {
                  unlink_node (nenofex, child);

                  /* BUG - FIX */
                  if (changed_subformula->lca == lca_object_lca
                      && child->changed_ch_list_pos)
                    remove_changed_lca_child (nenofex, child);

                  add_node_to_child_list (nenofex, new_and, child);
                  new_and->size_subformula += (child)->size_subformula;

#ifndef NDEBUG
                  if (is_literal_node ((child)))
                    {
                      assert ((child)->occ_link.next ||
                              (child)->occ_link.prev
                              || (child) == (child)->lit->occ_list.first);
                    }
#endif
                }               /* end: for */

              /* BUG - FIX */
              if (changed_subformula->lca == lca_object_lca)
                {
                  if (changed_subformula->num_children == 0)
                    reset_changed_lca_object (nenofex);
                  else if (changed_subformula->num_children !=
                           num_changed_ch_before_relinking)
                    add_changed_lca_child (nenofex, split_or);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
                  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
                  assert (!changed_subformula->lca ||
                          changed_subformula->num_children >= 2);
                  assert (!changed_subformula->lca ||
                          changed_subformula->top_p ==
                          changed_subformula->children +
                          changed_subformula->num_children);
                }

              update_level (nenofex, new_and);  /* NOTE: merge copy / update level ? */

              Node *copy_and = copy_formula_mark_propagation (nenofex, new_and, var);   /* levels copied */
              add_node_to_child_list (nenofex, split_or, copy_and);

              add_lca_child (&changed_subformula_new, new_and);
              add_lca_child (&changed_subformula_new, copy_and);

              split_or->size_subformula += 2 * copy_and->size_subformula;
              update_size_subformula (nenofex, split_or->parent,
                                      1 + copy_and->size_subformula + 1);

              if (!set_existential_split_or ||
                  !post_expansion_flattening_check_for_cnf_form (nenofex->
                                                                 existential_split_or))
                {
                  nenofex->existential_split_or = 0;
                  assign_or_update_changed_subformula
                    (&changed_subformula_new);
                }
              else
                {
                  assert (nenofex->existential_split_or);
                }
            }
        }
      else
        {
          assert (0);
        }
    }

#ifndef NDEBUG
#if ASSERT_GRAPH_AFTER_EXP_BEFORE_PROP
  assert_expand_graph_integrity (nenofex);
  assert_all_subformula_sizes (nenofex);
#endif
#endif

#ifndef NDEBUG
#if ASSERT_SIZES_IN_EXPANSION
  unsigned int size_after_exp_before_prop =
    subformula_size (nenofex, nenofex->graph_root);
  assert (size_before_exp + inc_score == size_after_exp_before_prop ||
          (((int) (size_before_exp + inc_score - size_after_exp_before_prop)
            >= 0)
           && (size_before_exp + inc_score - size_after_exp_before_prop <=
               2)));
  /* inc_score expected to be EXACT but found pessimistic (->ignore) off-by-one/two errors */
#endif
#endif

  assert (!nenofex->atpg_rr_called);

  if (!preemptive_occ)
    propagate_literals (nenofex, var);  /* graph should be fully reduced and cleaned up */
  else
    {
      assert (contained_lit == preemptive_occ);
      assert (preemptive_occ->parent);
      assert (is_or_node (preemptive_occ->parent));
      propagate_truth (nenofex, preemptive_occ);
    }

#ifndef NDEBUG
#if ASSERT_SIZES_IN_EXPANSION
  unsigned int size_after_exp_after_prop =
    subformula_size (nenofex, nenofex->graph_root);
  int estimated_size = size_before_exp + inc_score - dec_score;
  assert ((int) size_after_exp_after_prop <= estimated_size);   /* upper bound */
#if PRINT_SIZE_CHECK_INFO
  fprintf (stderr, "\tDeviation - Actual size vs. estimated size: %d\n",
           estimated_size - (int) size_after_exp_after_prop);
  fprintf (stderr, "\tActual costs: %d\n\n",
           (size_after_exp_before_prop - size_before_exp) -
           (size_after_exp_before_prop - size_after_exp_after_prop));
#endif
  assert (size_after_exp_after_prop ==
          size_before_exp + (size_after_exp_before_prop - size_before_exp) -
          (size_after_exp_before_prop - size_after_exp_after_prop));
#endif
#endif

  var->eliminated = 1;
  assert (!variable_has_occs (var));

  if (lca_object->lca)
    {
      unlink_variable_from_lca_list (var);
      reset_lca_object (nenofex, var, lca_object, 1);
    }

#ifndef NDEBUG
#if ASSERT_GRAPH_AFTER_EXP_AFTER_PROP
  assert_all_child_occ_lists_integrity (nenofex);
  assert_all_occ_lists_integrity (nenofex);
  assert_all_subformula_sizes (nenofex);
#endif
#endif

  /* post-expansion flattening: 'split-or' node might also 
     have been deleted (and hence reset) during propagations */
  if (nenofex->existential_split_or)
    {
      assert (post_expansion_flattening_check_for_cnf_form
              (nenofex->existential_split_or));
      assert (nenofex->options.post_expansion_flattening_specified);

      post_expansion_flattening_reset_changed_new (&changed_subformula_new);
      changed_subformula_new.lca = nenofex->existential_split_or->parent;

      post_expansion_flattening (nenofex, &changed_subformula_new);

      if (changed_subformula_new.num_children == 0)
        {
          post_expansion_flattening_reset_changed_new
            (&changed_subformula_new);
        }
      else if (changed_subformula_new.num_children == 1)
        {
          Node *remaining_child = changed_subformula_new.children[0];
          assert (remaining_child);
          post_expansion_flattening_reset_changed_new
            (&changed_subformula_new);
          if (!is_literal_node (remaining_child))
            {
              changed_subformula_new.lca = remaining_child;
              Node *child;
              for (child = remaining_child->child_list.first;
                   child; child = child->level_link.next)
                {
                  add_lca_child (&changed_subformula_new, child);
                }
              assign_or_update_changed_subformula (&changed_subformula_new);
            }
        }
      else
        {
          assign_or_update_changed_subformula (&changed_subformula_new);
        }

      nenofex->existential_split_or = 0;
    }

  nenofex->cur_expanded_var = 0;
  free_lca_children (&changed_subformula_new);
  assert (!nenofex->existential_split_or);
}


#ifndef NDEBUG
static void
assert_all_depending_vars_cleaned_up (Nenofex * nenofex)
{
  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;

      void **v_var, **var_end;
      var_end = scope->vars->top;
      for (v_var = scope->vars->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;
          assert (!var->collected_as_depending);
          assert (!var->copied);
        }                       /* end: for all vars */

    }                           /* end: for all scopes */
}
#endif


/*
- very similar to 'expand_existential_variable'
*/
static void
expand_universal_variable (Nenofex * nenofex, Var * var)
{
  assert (var);
  assert (is_universal_scope (var->scope));
  assert (!nenofex->cur_expanded_var);
  assert (!nenofex->existential_split_or);
  assert (variable_has_occs (var));

  nenofex->cur_expanded_var = var;
  LCAObject *lca_object = &var->exp_costs.lca_object;
  Node *lca_object_lca = lca_object->lca;

  LCAObject changed_subformula_new;
  init_lca_object (&changed_subformula_new);

  assert (lca_object_lca);
  assert (lca_object->num_children != 0 || is_literal_node (lca_object_lca));
  assert (!is_literal_node (lca_object_lca) || lca_object->num_children == 0);

  /* flags for revised expansion */
  Node *contained_lit = lca_children_contain_lit (var, lca_object);
  assert (!contained_lit || contained_lit->lit->var == var);
  Node *preemptive_occ = 0;

#ifndef NDEBUG
#if ASSERT_SIZES_IN_EXPANSION
  unsigned int size_before_exp =
    subformula_size (nenofex, nenofex->graph_root);
  unsigned int inc_score = var->exp_costs.inc_score;
  unsigned int dec_score = var->exp_costs.dec_score;
  int check_size = size_before_exp + inc_score - dec_score;
  assert (check_size >= 0);
#endif
#endif

  unsigned int num_children = lca_object_lca->num_children;
  NodeType type = lca_object_lca->type;
  int update_size_delta = 0;

  if (num_children == lca_object->num_children)
    {                           /* CASE 1 */
      if (type == NODE_TYPE_AND)
        {                       /* CASE 1.1 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_A_AND_ALL++;
#endif

          /* append copy of children */
          if (!contained_lit)
            {
              changed_subformula_new.lca = lca_object_lca;

              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {
                  Node *copy =
                    copy_formula_mark_propagation (nenofex, child, var);
                  add_node_to_child_list (nenofex, lca_object_lca, copy);
                  update_size_delta += copy->size_subformula;
                  assert (!is_literal_node ((child))
                          || (child)->lit->var == var);

                  add_lca_child (&changed_subformula_new, child);
                  add_lca_child (&changed_subformula_new, copy);
                }
              assert (2 * num_children == lca_object_lca->num_children);
              update_size_subformula (nenofex, lca_object_lca,
                                      update_size_delta);

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
          else
            {
              preemptive_occ = contained_lit;

              assign_or_update_changed_subformula (lca_object);
            }
        }
      else if (type == NODE_TYPE_OR)
        {                       /* CASE 1.2 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_A_OR_ALL++;
#endif

          if (contained_lit)
            {
              if (!contained_lit->lit->negated) /* TODO: remove branch, pass flag */
                mark_occs_propagation_var_cost_update (nenofex, var, 0);
              else
                mark_occs_propagation_var_cost_update (nenofex, var, 1);

              assign_or_update_changed_subformula (lca_object);
            }
          else if (lca_object_lca == nenofex->graph_root)
            {
              Node *split_and = and_node (nenofex);

              changed_subformula_new.lca = split_and;

              nenofex->graph_root = split_and;
              add_node_to_child_list (nenofex, split_and, lca_object_lca);
              update_level (nenofex, lca_object_lca);   /* TODO: merge copy / update_level */
              Node *copy =
                copy_formula_mark_propagation (nenofex, lca_object_lca, var);
              add_node_to_child_list (nenofex, split_and, copy);

              add_lca_child (&changed_subformula_new, lca_object_lca);
              add_lca_child (&changed_subformula_new, copy);

              nenofex->graph_root->size_subformula =
                1 + 2 * lca_object_lca->size_subformula;

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
          else                  /* average case */
            {
              assert (lca_object_lca->parent);
              assert (is_and_node (lca_object_lca->parent));

              changed_subformula_new.lca = lca_object_lca->parent;

              Node *copy =
                copy_formula_mark_propagation (nenofex, lca_object_lca, var);

              add_lca_child (&changed_subformula_new, lca_object_lca);
              add_lca_child (&changed_subformula_new, copy);

              add_node_to_child_list (nenofex, lca_object_lca->parent, copy);

              update_size_subformula (nenofex, lca_object_lca->parent,
                                      lca_object_lca->size_subformula);

              assert (!is_literal_node (copy));

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
        }
      else                      /* type == NODE_TYPE_LITERAL */
        {                       /* CONJECTURE: should not occur if all unates are fully eiminated */
          assert (!lca_object_lca->occ_link.next);      /* only 1 occ */
          assert (!lca_object_lca->occ_link.prev);

          if (!lca_object_lca->parent)
            {
              if (lca_object_lca->lit->negated)
                truth_propagation_mark (lca_object_lca);
            }
          else if (is_and_node (lca_object_lca->parent))
            {
              if (lca_object_lca->lit->negated)
                truth_propagation_mark (lca_object_lca);
            }
          else                  /* OR */
            {
              assert (is_or_node (lca_object_lca->parent));

              if (lca_object_lca->lit->negated)
                truth_propagation_mark (lca_object_lca);
            }

          assign_or_update_changed_subformula (lca_object);
        }
    }
  else
    {                           /* CASE 2: not all children of LCA need to be copied, only relevant ones */
      if (type == NODE_TYPE_AND)
        {                       /* CASE 2.1 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_A_AND_SUBSET++;
#endif

          if (!contained_lit)
            {
              changed_subformula_new.lca = lca_object_lca;

              /* append copy of children */
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {
                  Node *copy =
                    copy_formula_mark_propagation (nenofex, child, var);
                  add_node_to_child_list (nenofex, lca_object_lca, copy);
                  update_size_delta += copy->size_subformula;
                  assert (!is_literal_node ((child))
                          || (child)->lit->var == var);

                  add_lca_child (&changed_subformula_new, child);
                  add_lca_child (&changed_subformula_new, copy);
                }
              assert (num_children + lca_object->num_children ==
                      lca_object_lca->num_children);
              update_size_subformula (nenofex, lca_object_lca,
                                      update_size_delta);

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
          else
            {
              preemptive_occ = contained_lit;

              assign_or_update_changed_subformula (lca_object);
            }
        }
      else if (type == NODE_TYPE_OR)
        {                       /* CASE 2.2 */

#if COMPUTE_CASES_IN_EXPANSIONS
          nenofex->stats.num_exp_case_A_OR_SUBSET++;
#endif

          if (contained_lit)
            {
              if (!contained_lit->lit->negated) /* NOTE: remove branch, pass flag */
                mark_occs_propagation_var_cost_update (nenofex, var, 0);
              else
                mark_occs_propagation_var_cost_update (nenofex, var, 1);

              assign_or_update_changed_subformula (lca_object);
            }
          else
            {
              Node *split_and = and_node (nenofex);

              changed_subformula_new.lca = split_and;

              split_and->size_subformula = 1;
              add_node_to_child_list (nenofex, lca_object_lca, split_and);
              assert (split_and->level == lca_object_lca->level + 1);

              Node *new_or = or_node (nenofex);
              new_or->size_subformula = 1;
              add_node_to_child_list (nenofex, split_and, new_or);
              assert (new_or->level == split_and->level + 1);

              /* BUG - FIX */
              LCAObject *changed_subformula = &(nenofex->changed_subformula);
              unsigned int num_changed_ch_before_relinking =
                changed_subformula->num_children;
              assert (!changed_subformula->lca
                      || changed_subformula->num_children >= 2);

              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {
                  unlink_node (nenofex, child);

                  /* BUG - FIX */
                  if (changed_subformula->lca == lca_object_lca
                      && (child)->changed_ch_list_pos)
                    remove_changed_lca_child (nenofex, child);

                  add_node_to_child_list (nenofex, new_or, child);
                  new_or->size_subformula += (child)->size_subformula;

#ifndef NDEBUG
                  if (is_literal_node ((child)))
                    {
                      assert ((child)->occ_link.next ||
                              (child)->occ_link.prev
                              || (child) == (child)->lit->occ_list.first);
                    }
#endif
                }

              /* BUG - FIX */
              if (changed_subformula->lca == lca_object_lca)
                {
                  if (changed_subformula->num_children == 0)
                    reset_changed_lca_object (nenofex);
                  else if (changed_subformula->num_children !=
                           num_changed_ch_before_relinking)
                    add_changed_lca_child (nenofex, split_and);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
                  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif
                  assert (!changed_subformula->lca
                          || changed_subformula->num_children >= 2);
                  assert (!changed_subformula->lca
                          || changed_subformula->top_p ==
                          changed_subformula->children +
                          changed_subformula->num_children);
                }

              update_level (nenofex, new_or);   /* NOTE: merge copy / update_level ? */

              Node *copy_or =
                copy_formula_mark_propagation (nenofex, new_or, var);
              add_node_to_child_list (nenofex, split_and, copy_or);

              add_lca_child (&changed_subformula_new, new_or);
              add_lca_child (&changed_subformula_new, copy_or);

              split_and->size_subformula += 2 * copy_or->size_subformula;
              update_size_subformula (nenofex, split_and->parent,
                                      1 + copy_or->size_subformula + 1);

              assign_or_update_changed_subformula (&changed_subformula_new);
            }
        }
      else
        {
          assert (0);
        }
    }

#ifndef NDEBUG
#if ASSERT_GRAPH_AFTER_EXP_BEFORE_PROP
  assert_expand_graph_integrity (nenofex);
  assert_all_subformula_sizes (nenofex);
#endif
#endif

#ifndef NDEBUG
#if ASSERT_SIZES_IN_EXPANSION
  unsigned int size_after_exp_before_prop =
    subformula_size (nenofex, nenofex->graph_root);
  assert (size_before_exp + inc_score == size_after_exp_before_prop ||
          (((int) (size_before_exp + inc_score - size_after_exp_before_prop)
            >= 0)
           && (size_before_exp + inc_score - size_after_exp_before_prop <=
               2)));
  /* inc_score expected to be EXACT but we found pessimistic (->ignore) off-by-one/two errors */
#endif
#endif

  assert (!nenofex->atpg_rr_called);

  if (!preemptive_occ)
    propagate_literals (nenofex, var);  /* graph should be fully reduced and cleaned up */
  else
    {
      assert (contained_lit == preemptive_occ);
      assert (preemptive_occ->parent);
      assert (is_and_node (preemptive_occ->parent));
      propagate_falsity (nenofex, preemptive_occ);
    }

#ifndef NDEBUG
#if ASSERT_SIZES_IN_EXPANSION
  unsigned int size_after_exp_after_prop =
    subformula_size (nenofex, nenofex->graph_root);
  int estimated_size = size_before_exp + inc_score - dec_score;
  assert ((int) size_after_exp_after_prop <= estimated_size);   /* upper bound */
#if PRINT_SIZE_CHECK_INFO
  fprintf (stderr, "\tDeviation - Actual size vs. estimated size: %d\n",
           estimated_size - (int) size_after_exp_after_prop);
  fprintf (stderr, "\tActual costs: %d\n\n",
           (size_after_exp_before_prop - size_before_exp) -
           (size_after_exp_before_prop - size_after_exp_after_prop));
#endif
  assert (size_after_exp_after_prop == size_before_exp +
          (size_after_exp_before_prop - size_before_exp) -
          (size_after_exp_before_prop - size_after_exp_after_prop));
#endif
#endif

  var->eliminated = 1;
  assert (!variable_has_occs (var));

  if (lca_object->lca)
    {
      unlink_variable_from_lca_list (var);
      reset_lca_object (nenofex, var, lca_object, 1);
    }

#ifndef NDEBUG
#if ASSERT_GRAPH_AFTER_EXP_AFTER_PROP
  assert_all_child_occ_lists_integrity (nenofex);
  assert_all_occ_lists_integrity (nenofex);
  assert_all_subformula_sizes (nenofex);
#endif
#endif

  nenofex->cur_expanded_var = 0;
  free_lca_children (&changed_subformula_new);
  assert (!nenofex->existential_split_or);

  if (count_stack (nenofex->depending_vars))
    {                           /* expansion of non-innermost universal variable -> need to clean up dep. vars */
      assert (var->scope == (*nenofex->next_scope));
      assert (is_existential_scope (*nenofex->cur_scope));

      Var *depending_var;
      while ((depending_var = pop_stack (nenofex->depending_vars)))
        {
          assert (is_existential_scope (depending_var->scope));
          assert (depending_var->scope->nesting >
                  (*nenofex->next_scope)->nesting);
          assert (depending_var->collected_as_depending);
          assert (depending_var->copied);

          depending_var->collected_as_depending = 0;
          depending_var->copied = 0;
        }                       /* end while stack not empty */

#ifndef NDEBUG
#if ASSERT_ALL_DEPENDING_VARS_CLEANED_UP
      assert_all_depending_vars_cleaned_up (nenofex);
#endif
#endif
    }                           /* end: clean up depending variables */
}


/* 
- determines number of nodes to be created when expanding variable 'var'
- type of LCA and type of var influence this value (8 cases)
*/
static unsigned int
expansion_increase_score (Nenofex * nenofex, Var * var,
                          LCAObject * lca_object)
{
  assert (lca_object->lca);
  unsigned int inc_score = 0;

#ifndef NDEBUG
  Node **ch_test, *child_test;
  unsigned int test_score = 0;
  for (ch_test = lca_object->children; (child_test = *ch_test); ch_test++)
    {
      test_score++;
      if (is_literal_node ((child_test)))
        assert ((is_universal_scope (var->scope)
                 && var->scope->nesting < (*nenofex->cur_scope)->nesting)
                || (child_test)->lit->var == var);
    }
  assert (test_score == lca_object->num_children);
#endif

  Node *lca = lca_object->lca;
  NodeType lca_type = lca->type;

  assert (!is_literal_node (lca) || !lca_object->children[0]);

  /* special case where LCA-children contain literal (of var to be expanded) */
  if (lca_children_contain_lit (var, lca_object) || is_literal_node (lca))
    {
      assert (inc_score == 0);
#if COMPUTE_NUM_NON_INC_EXPANSIONS_IN_SCORES
      nenofex->stats.num_non_inc_expansions_in_scores++;
#endif
      return inc_score;
    }

  assert (!is_literal_node (lca));

  /* increment 'inc_score' depending on following situations */
  if (is_existential_scope (var->scope))
    {
      if (lca_object->num_children == lca->num_children)        /* copy all */
        {
          if (lca_type == NODE_TYPE_AND)
            {
              inc_score += lca->size_subformula;
              if (lca == nenofex->graph_root)   /* create split-OR in this case */
                inc_score++;
            }
          else                  /* OR */
            {
              assert (lca_type == NODE_TYPE_OR);
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {               /* count subformula-sizes of lca's relevant children */
                  inc_score += (child)->size_subformula;
                }               /* end: for all children */
            }
        }
      else                      /* copy only lca's relevant children */
        {
          if (lca->type == NODE_TYPE_OR)
            {
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {               /* count subformula-sizes of lca's relevant children */
                  inc_score += (child)->size_subformula;
                }               /* end: for all children */
            }
          else if (lca_type == NODE_TYPE_AND)
            {
              /* add split-or and 2 new and-nodes holding copied children of lca */
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {               /* count subformula-sizes of lca's relevant children */
                  inc_score += (child)->size_subformula;
                }               /* end: for all children */
              inc_score += 3;   /* add split-or + two new ANDs */
            }
        }
    }
  else                          /* universal variables */
    {
      if (lca_object->num_children == lca->num_children)        /* copy all */
        {
          if (lca_type == NODE_TYPE_OR)
            {
              inc_score += lca->size_subformula;
              if (lca == nenofex->graph_root)   /* have to create split-AND in this case */
                inc_score++;
            }
          else                  /* AND */
            {
              assert (lca_type == NODE_TYPE_AND);
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {               /* count subformula-sizes of lca's relevant children */
                  inc_score += (child)->size_subformula;
                }               /* end: for all children */
            }
        }
      else                      /* copy only lca's relevant children */
        {
          if (lca->type == NODE_TYPE_AND)
            {
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {               /* count subformula-sizes of lca's relevant children */
                  inc_score += (child)->size_subformula;
                }               /* end: for all children */
            }
          else if (lca_type == NODE_TYPE_OR)
            {
              /* add split-or and 2 new and-nodes holding copied children of lca */
              Node **ch, *child;
              for (ch = lca_object->children; (child = *ch); ch++)
                {               /* count subformula-sizes of lca's relevant children */
                  inc_score += (child)->size_subformula;
                }               /* end: for all children */
              inc_score += 3;   /* add split-and + two new ORs */
            }
        }
    }                           /* end: universal variables */

  return inc_score;
}


/*
- count nodes that will be deleted when setting variable to false
*/
static void
decrease_score_propagate_falsity (Nenofex * nenofex, Node * occ,
                                  LCAObject * lca_object)
{
  assert (occ);
  assert (is_literal_node (occ));

  if (occ == nenofex->graph_root)
    {
      assert (!decrease_score_marked (occ));
      decrease_score_mark (occ);
      return;
    }

  Node *occ_parent = occ->parent;
  assert (occ_parent);

  Node *lca_object_lca = lca_object->lca;

  if (is_and_node (occ_parent))
    {
      if (is_existential_scope (occ->lit->var->scope))
        {
          if (occ_parent == lca_object_lca)
            {
              assert (!decrease_score_marked (occ_parent));
            }
          else if (occ == lca_object_lca)
            {
              assert (is_literal_node (lca_object_lca));
            }
          else
            {
              decrease_score_mark (occ_parent);
            }
        }                       /* end: existential var. */
      else
        {
          decrease_score_mark (occ_parent);
        }
    }
  else                          /* parent is OR */
    {
      decrease_score_mark (occ);
    }
}


/*
- count nodes that will be deleted when setting variable to true
*/
static void
decrease_score_propagate_truth (Nenofex * nenofex, Node * occ,
                                LCAObject * lca_object)
{
  assert (occ);
  assert (is_literal_node (occ));

  if (occ == nenofex->graph_root)
    {
      assert (!decrease_score_marked (occ));
      decrease_score_mark (occ);
      return;
    }

  Node *occ_parent = occ->parent;
  assert (occ_parent);

  Node *lca_object_lca = lca_object->lca;

  if (is_or_node (occ_parent))
    {
      if (is_universal_scope (occ->lit->var->scope))
        {
          if (occ_parent == lca_object_lca)
            {
              assert (!decrease_score_marked (occ_parent));
            }
          else if (occ == lca_object_lca)
            {
              assert (is_literal_node (lca_object_lca));
            }
          else
            {
              decrease_score_mark (occ_parent);
            }
        }                       /* end: universal vars */
      else
        {
          decrease_score_mark (occ_parent);
        }
    }
  else                          /* parent is AND */
    {
      decrease_score_mark (occ);
    }
}


/*
- set variable 'virtually' to true/false and count node decrease
- nodes which have been marked would have been deleted by propagation
- collect and unmark nodes bottom-up
- pessimistic score; might lose more by parent-merging or 1-l-simp
*/
static unsigned int
decrease_score_count_deleted (Nenofex * nenofex, Var * var)
{
  unsigned int num_deleted = 0;

  Lit *lit = var->lits;
  Node *var_lca = var->exp_costs.lca_object.lca;
  assert (var_lca);

  Stack *collected_nodes = create_stack (DEFAULT_STACK_SIZE);

  Node *occ;

AGAIN:

  for (occ = lit->occ_list.first; occ; occ = occ->occ_link.next)
    {
      Node *cur, *highest_marked;

      cur = occ;
      assert (is_literal_node (cur));

      if (!decrease_score_marked (cur))
        {
          cur = cur->parent;
          if (!cur)             /* special case: only one occ which is at root */
            {
              assert (0);
            }
          else if (!decrease_score_marked (cur))
            {                   /* parent already unmarked -> there is a collected node on path up to root */
              continue;
            }
        }                       /* end: if occ not marked */
      assert ((is_literal_node (cur) && decrease_score_marked (cur)) ||
              (!is_literal_node (cur) && decrease_score_marked (cur)));

      highest_marked = cur;
      cur = cur->parent;

      unsigned const int lowest_level =
        var_lca->parent ? var_lca->parent->level : var_lca->level;

      int found_collected = 0;
      while (cur && cur->level >= lowest_level) /* NOTE: strengthen loop condition ? */
        {
          assert (!is_literal_node (cur));

          if (decrease_score_marked (cur))
            {
              assert (!decrease_score_collected_marked (cur));
              decrease_score_unmark (highest_marked);
              highest_marked = cur;
            }
          else if (decrease_score_collected_marked (cur))
            {
              found_collected = 1;
              break;
            }

          cur = cur->parent;
        }                       /* end: while */

      if (found_collected)
        {
          decrease_score_unmark (highest_marked);
        }
      else
        {
          decrease_score_unmark (highest_marked);
          assert (!decrease_score_collected_marked (highest_marked));
          decrease_score_collected_mark (highest_marked);
          push_stack (collected_nodes, highest_marked);
        }
    }                           /* end: for all occurrences */

  if (lit == var->lits)
    {
      lit++;
      assert (lit == var->lits + 1);
      goto AGAIN;               /* visit positive occurrences */
    }

  Node *collected_node;
  while ((collected_node = pop_stack (collected_nodes)))
    {
      assert (decrease_score_collected_marked (collected_node));
      decrease_score_collected_unmark (collected_node);
      num_deleted += collected_node->size_subformula;
    }                           /* end: while */

  delete_stack (collected_nodes);

  return num_deleted;
}


/*
- determine how many nodes will be deleted if expanding var and propagating literals
- TODO: REVISION NECESSARY (cases of non-increasing expansions properly captured?)
*/

#define REVISION 1

static unsigned int
expansion_decrease_score (Nenofex * nenofex, Var * var,
                          LCAObject * lca_object)
{
  unsigned int dec_score = 0;

  Node *occ;
  Lit *lit = var->lits;
  assert (lit->negated);

  Node *contained_lit = lca_children_contain_lit (var, lca_object);
  assert (!contained_lit || contained_lit->lit->var == var);

  assert (!contained_lit || !is_literal_node (lca_object->lca));

  if (contained_lit || is_literal_node (lca_object->lca))
    {

#if !REVISION
      if ((contained_lit && !contained_lit->lit->negated) ||
          (!contained_lit && !lca_object->lca->lit->negated))
        {
          goto TRUTH_PROPAGATION;
        }
#else
      if ((contained_lit && !contained_lit->lit->negated
           && is_existential_scope (var->scope)) || (contained_lit
                                                     && contained_lit->lit->
                                                     negated
                                                     &&
                                                     is_universal_scope (var->
                                                                         scope))
          || (!contained_lit && !lca_object->lca->lit->negated
              && is_existential_scope (var->scope)) || (!contained_lit
                                                        && lca_object->lca->
                                                        lit->negated
                                                        &&
                                                        is_universal_scope
                                                        (var->scope)))
        {
          goto TRUTH_PROPAGATION;
        }
#endif
    }

  /* for all neg. occs: pretend to propagate falsity and count node decrease */
  for (occ = lit->occ_list.first; occ; occ = occ->occ_link.next)
    {
      decrease_score_propagate_truth (nenofex, occ, lca_object);
    }
  lit = var->lits + 1;
  assert (!lit->negated);
  /* similarly for all positve occurrences */
  for (occ = lit->occ_list.first; occ; occ = occ->occ_link.next)
    {
      decrease_score_propagate_falsity (nenofex, occ, lca_object);
    }

  dec_score += decrease_score_count_deleted (nenofex, var);

  if (contained_lit || is_literal_node (lca_object->lca))
    {
#if !REVISION
      if ((contained_lit && contained_lit->lit->negated) ||
          (!contained_lit && lca_object->lca->lit->negated))
        {
          goto FINISHED;
        }
#else
      if ((contained_lit && !contained_lit->lit->negated
           && is_universal_scope (var->scope)) || (contained_lit
                                                   && contained_lit->lit->
                                                   negated
                                                   &&
                                                   is_existential_scope (var->
                                                                         scope))
          || (!contained_lit && !lca_object->lca->lit->negated
              && is_universal_scope (var->scope)) || (!contained_lit
                                                      && lca_object->lca->
                                                      lit->negated
                                                      &&
                                                      is_existential_scope
                                                      (var->scope)))
        {
          goto FINISHED;
        }
#endif
    }

TRUTH_PROPAGATION:

#ifndef NDEBUG
#if ASSERT_GRAPH_BETWEEN_DEC_SCORE
  assert_all_child_occ_lists_integrity (nenofex);
  assert_all_occ_lists_integrity (nenofex);
#endif
#endif

  lit = var->lits;
  /* for all neg. occs: pretend to propagate truth and count node decrease */
  for (occ = lit->occ_list.first; occ; occ = occ->occ_link.next)
    {
      decrease_score_propagate_falsity (nenofex, occ, lca_object);
    }
  lit = var->lits + 1;
  /* similarly for all positve occurrences */
  for (occ = lit->occ_list.first; occ; occ = occ->occ_link.next)
    {
      decrease_score_propagate_truth (nenofex, occ, lca_object);
    }

  dec_score += decrease_score_count_deleted (nenofex, var);

FINISHED:

#ifndef NDEBUG
#if ASSERT_GRAPH_AFTER_DEC_SCORE
  assert_all_child_occ_lists_integrity (nenofex);
  assert_all_occ_lists_integrity (nenofex);
#endif
#endif

  return dec_score;
}


/* 
- compute LCA and scores for variable according to marks set
*/
static void
init_variable_scores (Nenofex * nenofex, Var * var,
                      const int update_key_priority_queue)
{
  if (!variable_has_occs (var))
    {
      return;
    }

  int old_score = var->exp_costs.score;

  /*assert(variable_has_occs(var)); */

  assert (!lca_update_marked (var) ||
          (inc_score_update_marked (var) && dec_score_update_marked (var)));

  if (lca_update_marked (var))
    {
      if (var->exp_costs.lca_object.lca)
        {
          assert (var->exp_costs.lca_object.lca->var_lca_list.first);
          assert (var->exp_costs.lca_object.lca->var_lca_list.last);

          unlink_variable_from_lca_list (var);
          reset_lca_object (nenofex, var, &var->exp_costs.lca_object, 1);
        }

      if (is_universal_scope (var->scope) && nenofex->next_scope &&
          var->scope == *nenofex->next_scope && nenofex->cur_scope &&
          is_existential_scope (*nenofex->cur_scope))
        {
          find_non_innermost_universal_lca_and_children (nenofex, var);
        }
      else
        {
          find_variable_lca_and_children (nenofex, var,
                                          &var->exp_costs.lca_object, 1);
        }

#ifndef NDEBUG
#if ASSERT_INIT_VAR_SCORES_ALL_UNMARKED
      assert_all_lca_children_unmarked (&var->exp_costs.lca_object);
#endif
#endif
    }                           /* end: variable marked for lca update */

  if (inc_score_update_marked (var))
    var->exp_costs.inc_score =
      expansion_increase_score (nenofex, var, &var->exp_costs.lca_object);

  if (dec_score_update_marked (var))
    var->exp_costs.dec_score =
      expansion_decrease_score (nenofex, var, &var->exp_costs.lca_object);

  if (inc_score_update_marked (var) || dec_score_update_marked (var))
    var->exp_costs.score =
      var->exp_costs.inc_score - var->exp_costs.dec_score;

  if (update_key_priority_queue)
    update_key (var->scope->priority_heap, var, old_score);
}


static void
add_lit_node_to_occurrence_list (Nenofex * nenofex, Node * new_occ)
{
  assert (is_literal_node (new_occ));
  assert (new_occ);
  assert (!new_occ->occ_link.next);
  assert (!new_occ->occ_link.prev);

#ifndef NDEBUG
  Lit *lit_p = new_occ->lit;
  assert (lit_p);
  if (lit_p->negated)
    assert (lit_p == lit_p->var->lits);
  else
    assert (lit_p == lit_p->var->lits + 1);
#endif

  Lit *lit = new_occ->lit;
  assert (lit);

  if (!lit->occ_list.first)
    {                           /* first occurrence */
      assert (!lit->occ_list.last);

      lit->occ_list.first = lit->occ_list.last = new_occ;
    }
  else
    {                           /* append */
      assert (lit->occ_list.last);

      lit->occ_list.last->occ_link.next = new_occ;
      new_occ->occ_link.prev = lit->occ_list.last;
      lit->occ_list.last = new_occ;

      assert (!new_occ->occ_link.next);
    }

  lit->occ_cnt++;

  assert (new_occ->occ_link.next || new_occ->lit->occ_list.last == new_occ);
  assert (new_occ->occ_link.prev || new_occ->lit->occ_list.first == new_occ);
}


static void
add_node_to_child_list_before (Nenofex * nenofex, Node * child,
                               Node * new_child);


/*
- introduced convention that literals are stored first in child list
*/
static void
add_lit_node_to_child_list (Nenofex * nenofex, Node * parent,
                            Node * lit_child)
{
  assert (parent->child_list.first);
  assert (parent->child_list.last);
  assert (parent->num_children >= 1);
  assert (is_literal_node (lit_child));

  add_node_to_child_list_before (nenofex, parent->child_list.first,
                                 lit_child);
}


void
add_node_to_child_list (Nenofex * nenofex, Node * parent, Node * new_child)
{
  assert (parent);
  assert (new_child);
  assert (!new_child->level_link.next);
  assert (!new_child->level_link.prev);
  assert (!is_literal_node (parent));

  /* introduced convention that literals are stored first in child list */
  if (is_literal_node (new_child) && parent->child_list.first)
    {                           /* parent has at least 1 child */
      add_lit_node_to_child_list (nenofex, parent, new_child);
      return;
    }

  if (!parent->child_list.first)
    {                           /* 'new_child' is the first child in list */
      assert (!parent->child_list.last);

      parent->child_list.first = parent->child_list.last = new_child;
      new_child->parent = parent;
      new_child->level = parent->level + 1;
      parent->num_children++;
    }
  else
    {                           /* append 'new_child' to list */
      assert (parent->child_list.last);
      assert (!parent->child_list.last->level_link.next);

      parent->child_list.last->level_link.next = new_child;
      new_child->level_link.prev = parent->child_list.last;
      parent->child_list.last = new_child;
      new_child->parent = parent;
      new_child->level = parent->level + 1;
      parent->num_children++;
    }

  assert (new_child->level == parent->level + 1);
  assert (!new_child->level_link.next);
}


/* inserts 'new_child' before 'child' in child list */
static void
add_node_to_child_list_before (Nenofex * nenofex, Node * child,
                               Node * new_child)
{
  Node *parent = child->parent;

  assert (child);
  assert (parent);
  assert (new_child);
  assert (!new_child->level_link.next);
  assert (!new_child->level_link.prev);
  assert (!is_literal_node (parent));

  if (!parent->child_list.first)
    {                           /* 'new_child' is the first child in list */
      assert (!parent->child_list.last);

      parent->child_list.first = parent->child_list.last = new_child;
      new_child->parent = parent;
      new_child->level = parent->level + 1;

      assert (!new_child->level_link.next);
    }
  else
    {                           /* insert 'new_child' to list */
      assert (parent->child_list.last);
      assert (!parent->child_list.last->level_link.next);

      new_child->level_link.next = child;
      new_child->level_link.prev = child->level_link.prev;
      child->level_link.prev = new_child;

      if (new_child->level_link.prev)
        {                       /* standard case */
          assert (child != parent->child_list.first);

          new_child->level_link.prev->level_link.next = new_child;
        }
      else
        {                       /* prepending */
          assert (child == parent->child_list.first);

          parent->child_list.first = new_child;
        }

      new_child->parent = parent;
      new_child->level = parent->level + 1;
    }
  parent->num_children++;

  assert (new_child->level == parent->level + 1);
}


/*
- TODO: refinements
*/
static Node *
add_orig_clause (Nenofex * nenofex, Stack * lit_stack)
{
  assert (nenofex->graph_root);
  assert (nenofex->graph_root->size_subformula);

  unsigned int lit_cnt = count_stack (lit_stack);

  if (lit_cnt == 1)
    {                           /* adding unit clause */
      unsigned long int abs_lit = (((long int) (lit_stack->elems[0])) < 0 ?
                                   -((long int) (lit_stack->
                                                 elems[0])) : ((long
                                                                int)
                                                               (lit_stack->
                                                                elems[0])));

      if (!nenofex->vars[abs_lit])
        {
          if (count_stack (nenofex->scopes) != 1)
            fprintf (stderr,
                     "WARNING: first occ. of var in a clause in formula which is NOT propositional!\n");
          init_variable (nenofex, abs_lit, 0);
        }

      Node *lit_n =
        lit_node (nenofex, ((long int) lit_stack->elems[0]),
                  nenofex->vars[abs_lit]);
      lit_n->size_subformula = 1;
      add_node_to_child_list (nenofex, nenofex->graph_root, lit_n);
      nenofex->graph_root->size_subformula++;
      add_lit_node_to_occurrence_list (nenofex, lit_n);

      return lit_n;
    }                           /* end: adding unit clause */

  Node *clause = or_node (nenofex);
  add_node_to_child_list (nenofex, nenofex->graph_root, clause);
  clause->size_subformula = 1;

  unsigned int i;
  for (i = 0; i < lit_cnt; i++)
    {
      long int lit = (long int) (lit_stack->elems[i]);
      unsigned long int abs_lit = (lit < 0 ? -lit : lit);

      if (!nenofex->vars[abs_lit])
        {
          if (count_stack (nenofex->scopes) != 1)
            fprintf (stderr,
                     "WARNING: first occ. of var in a clause in formula which is NOT propositional!\n");
          init_variable (nenofex, abs_lit, 0);
        }

      Node *lit_n = lit_node (nenofex, lit, nenofex->vars[abs_lit]);
      lit_n->size_subformula = 1;
      add_node_to_child_list (nenofex, clause, lit_n);
      add_lit_node_to_occurrence_list (nenofex, lit_n);
    }                           /* end: for all literals */
  clause->size_subformula += lit_cnt;

  nenofex->graph_root->size_subformula += clause->size_subformula;

  /* keep first two clauses -> simplify after all clauses have been parsed */
  if (nenofex->graph_root->num_children >= 3)
    {
      simplify_one_level (nenofex, clause);
    }

  return clause;
}


static void
delete_scope (Scope * scope)
{
  delete_stack (scope->vars);
  delete_priority_queue (&scope->priority_heap);
  mem_free (scope, sizeof (Scope));
}


static Scope *
new_scope ()
{
  size_t bytes = sizeof (Scope);
  Scope *result = (Scope *) mem_malloc (bytes);
  assert (result);
  memset (result, 0, bytes);

  result->vars = create_stack (DEFAULT_STACK_SIZE);
  create_priority_queue (&result->priority_heap, DEFAULT_STACK_SIZE);

  return result;
}


/*
- TODO: refinements
*/
static void
add_orig_scope (Nenofex * nenofex, Stack * lit_stack,
                ScopeType parsed_scope_type)
{
  Scope *scope = new_scope ();
  scope->type = parsed_scope_type;
  scope->nesting = count_stack (nenofex->scopes);

  /* TODO: handle empty scope */
  assert (count_stack (lit_stack));

  unsigned int i;
  unsigned int cnt = count_stack (lit_stack);
  for (i = 0; i < cnt; i++)
    {                           /* TODO: pop instead */
      long int lit = (long int) (lit_stack->elems[i]);
      if (lit < 0)
        {
          fprintf (stderr, "Variable %ld quantified negatively!\n", lit);
          exit (1);
        }

      if (nenofex->vars[lit])
        {
          fprintf (stderr, "Variable %ld already quantified!\n", lit);
          exit (1);
        }

      init_variable (nenofex, lit, scope);
    }

  push_stack (nenofex->scopes, scope);
}


static void
set_cnf_root (Nenofex * nenofex)
{
  Node *and = and_node (nenofex);
  and->size_subformula = 1;     /* will be incremented when adding clauses */
  nenofex->graph_root = and;
}


static void
add_default_scope (Nenofex * nenofex)
{
  Scope *default_scope = new_scope ();
  default_scope->type = SCOPE_TYPE_EXISTENTIAL;
  default_scope->nesting = DEFAULT_SCOPE_NESTING;

  push_stack (nenofex->scopes, default_scope);

  assert (nenofex->scopes->elems[0] == default_scope);
}


static void
set_up_preamble (Nenofex * nenofex, unsigned int num_vars,
                 unsigned int num_clauses)
{
  size_t bytes = (num_vars + 1) * sizeof (Var *);
  nenofex->vars = (Var **) mem_malloc (bytes);
  assert (nenofex->vars);
  memset (nenofex->vars, 0, bytes);

  nenofex->num_orig_vars = num_vars;
  nenofex->next_free_node_id = num_vars + 1;
  nenofex->tseitin_next_id = num_vars + 1;
  nenofex->num_orig_clauses = num_clauses;
  set_cnf_root (nenofex);
}


static int is_unsigned_string (char *str);


/*
- TODO: refinements
- NOTE: root need not be simplified by one-level-simplification 
*/
static int
parse (Nenofex * nenofex, FILE * input_file)
{
  unsigned int clause_cnt = 0;
  int closed = 1;
  int preamble_found = 0;

  int done = 0;

  add_default_scope (nenofex);

  ScopeType parsed_scope_type = 0;

  Node *first_clause = 0;
  Node *second_clause = 0;
  Stack *lit_stack = create_stack (DEFAULT_STACK_SIZE);

  char c;
  while ((c = fgetc (input_file)) != EOF)
    {
      assert (nenofex->result == NENOFEX_RESULT_UNKNOWN);

      if (c == 'c')
        {
          while ((c = fgetc (input_file)) != EOF && c != '\n')
            ;
        }
      else if (c == 'p')
        {
          if (preamble_found)
            {
              fprintf (stderr, "Preamble already occurred!\n\n");
              exit (1);
            }

          if ((c = fgetc (input_file)) != ' ' ||
              (c = fgetc (input_file)) != 'c' ||
              (c = fgetc (input_file)) != 'n' ||
              (c = fgetc (input_file)) != 'f' ||
              (c = fgetc (input_file)) != ' ')
            {
              fprintf (stderr, "Malformed preamble!\n\n");
              exit (1);
            }

          char num_vars_str[128] = { '\0' };
          char num_clauses_str[128] = { '\0' };

          fscanf (input_file, "%s", num_vars_str);
          fscanf (input_file, "%s", num_clauses_str);

          if (!is_unsigned_string (num_vars_str)
              || !is_unsigned_string (num_clauses_str))
            {
              fprintf (stderr, "Malformed preamble!\n\n");
              exit (1);
            }

          unsigned int num_vars, num_clauses;
          num_vars = atoi (num_vars_str);
          num_clauses = atoi (num_clauses_str);

          set_up_preamble (nenofex, num_vars, num_clauses);

          preamble_found = 1;
        }
      else if (c == '-' || isdigit (c))
        {
          if (!preamble_found)
            {
              fprintf (stderr, "Preamble missing!\n\n");
              exit (1);
            }


          closed = 0;

          ungetc (c, input_file);

          long int val;
          fscanf (input_file, "%ld", &val);

          if (val == 0)
            {
              if (!parsed_scope_type)   /* parsing a clause */
                {
                  clause_cnt++;

                  if (clause_cnt > nenofex->num_orig_clauses)
                    {
                      fprintf (stderr, "Too many clauses!\n\n");
                      exit (1);
                    }
                  if (count_stack (lit_stack) == 0)
                    {           /* empty clause */
                      done = 1;
                      nenofex->result = NENOFEX_RESULT_UNSAT;
                      goto SKIP_SIMPLIFY;
                    }

                  Node *clause = add_orig_clause (nenofex, lit_stack);

                  /* first two clauses are not simplified after parsing 
                     because this could delete whole graph */
                  if (clause_cnt == 1)
                    {
                      assert (nenofex->graph_root->num_children == 1);
                      first_clause = clause;
                    }
                  else if (clause_cnt == 2)
                    {
                      assert (nenofex->graph_root->num_children == 2);
                      second_clause = clause;
                    }

                  /*clause_closed = 1; */
                }
              else              /* parsing a scope */
                {
                  add_orig_scope (nenofex, lit_stack, parsed_scope_type);
                  parsed_scope_type = 0;
                }

              closed = 1;
              reset_stack (lit_stack);
            }
          else
            {
              unsigned int check_val = (val < 0 ? -val : val);

              if (check_val > nenofex->num_orig_vars)
                {
                  fprintf (stderr, "Literal out of bounds!\n");
                  exit (1);
                }
              push_stack (lit_stack, (void *) val);
            }
        }
      else if (c == 'a')
        {
          parsed_scope_type = SCOPE_TYPE_UNIVERSAL;
          if (!closed)
            {
              fprintf (stderr, "Scope not closed!\n");
              exit (1);
            }
        }
      else if (c == 'e')
        {
          parsed_scope_type = SCOPE_TYPE_EXISTENTIAL;
          if (!closed)
            {
              fprintf (stderr, "Scope not closed!\n");
              exit (1);
            }
        }
      else if (!isspace (c))
        {
          fprintf (stderr, "Parsing: invalid character %c\n", c);
          exit (1);
        }

    }                           /* end: while not end of file */

  assert (nenofex->result == NENOFEX_RESULT_UNKNOWN);

  if (!preamble_found)
    {
      fprintf (stderr, "Preamble missing!\n");
      exit (1);
    }

  if (!closed)
    {
      fprintf (stderr, "Scope or clause not closed!\n");
      exit (1);
    }

  if (clause_cnt != nenofex->num_orig_clauses)
    {
      fprintf (stderr, "Numbers of clauses do not match!\n");
      exit (1);
    }

#if 0
  if (count_stack (nenofex->var_stack) != nenofex->num_orig_vars)
    {
      fprintf (stderr,
               "Warning: Number of variables does not match! specified %d, but actually %d\n",
               nenofex->num_orig_vars, count_stack (nenofex->var_stack));
    }
#endif

  if (clause_cnt == 0)
    {                           /* no clause parsed */
      done = 1;
      nenofex->result = NENOFEX_RESULT_SAT;
      goto SKIP_SIMPLIFY;
    }

  if (nenofex->graph_root->num_children == 1)
    {                           /* only one clause parsed -> becomes new graph root */
      assert (nenofex->graph_root->child_list.first);
      assert (nenofex->graph_root->child_list.first ==
              nenofex->graph_root->child_list.last);

      Node *clause = nenofex->graph_root->child_list.first;
      unlink_node (nenofex, clause);
      delete_node (nenofex, nenofex->graph_root);
      nenofex->graph_root = clause;

      clause->level = 0;

      if (!is_literal_node (clause))
        {
          Node *child;
          for (child = clause->child_list.first; child;
               child = child->level_link.next)
            child->level = 1;
        }

      first_clause = second_clause = 0; /* need not simplify afterwards */
    }                           /* end: root had only one clause */

  /* simplify first two clauses */
  assert (is_literal_node (nenofex->graph_root)
          || nenofex->graph_root->num_children >= 2);

  if (first_clause && !is_literal_node (first_clause))  /* if is not unit */
    {
      simplify_one_level (nenofex, first_clause);
      if (nenofex->graph_root && is_or_node (nenofex->graph_root))      /* only 1 non-unit left */
        simplify_one_level (nenofex, nenofex->graph_root);
      else if (nenofex->graph_root && !is_literal_node (second_clause)) /* if not unit */
        simplify_one_level (nenofex, second_clause);
    }
  else if (first_clause)        /* first is unit, check second */
    {
      if (second_clause && !is_literal_node (second_clause))    /* if not unit */
        simplify_one_level (nenofex, second_clause);
    }

  /* NOTE: unit elimination is performed -> actually need no 1-l-simp */
  if (nenofex->graph_root && !is_literal_node (nenofex->graph_root))
    simplify_one_level (nenofex, nenofex->graph_root);

SKIP_SIMPLIFY:

  mem_free (nenofex->vars, (nenofex->num_orig_vars + 1) * sizeof (Var *));
  nenofex->vars = (Var **) 0;

  delete_stack (lit_stack);

  return done;
}


static Var *
find_min_cost_var_in_scope (Nenofex * nenofex, Scope * scope)
{
  assert (!count_stack (nenofex->vars_marked_for_update));

  Var *min_cost_var = 0;
  Stack *scope_priority_heap = scope->priority_heap;

  while ((min_cost_var = remove_min (scope_priority_heap)))
    {
      if (variable_has_occs (min_cost_var))
        break;
      else
        {
          assert (0);
        }
    }

  nenofex->num_cur_remaining_scope_vars = count_stack (scope_priority_heap);

  return min_cost_var;
}


/*
- like 'find_min_cost_var_in_scope' but does not remove var from priority queue
*/
static Var *
peek_min_cost_var_in_scope (Nenofex * nenofex, Scope * scope)
{
  assert (!count_stack (nenofex->vars_marked_for_update));

  Var *min_cost_var = 0;
  Stack *scope_priority_heap = scope->priority_heap;

  while ((min_cost_var = access_min (scope_priority_heap)))
    {
      if (variable_has_occs (min_cost_var))
        break;
      else
        {
          assert (0);
        }
    }

  return min_cost_var;
}


#ifndef NDEBUG
/*
- for assertion checking only 
*/
static void
assert_all_subformula_sizes (Nenofex * nenofex)
{
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (is_literal_node (cur))
        {
          assert (cur->size_subformula == 1);
          cur->test_size_subformula = 1;
        }
      else
        {
          if (size_subformula_marked (cur))
            {
              size_subformula_unmark (cur);
              cur->test_size_subformula = 1;

              Node *child;
              for (child = cur->child_list.first; child;
                   child = child->level_link.next)
                cur->test_size_subformula += child->test_size_subformula;

              assert (cur->test_size_subformula == cur->size_subformula);
            }
          else                  /* recalculate sizes of children first */
            {
              size_subformula_mark (cur);
              push_stack (stack, cur);

              Node *child;
              for (child = cur->child_list.last; child;
                   child = child->level_link.prev)
                push_stack (stack, child);
            }
        }                       /* end: op-node */
    }                           /* end: while */

  delete_stack (stack);
}

#endif /* end ifndef NDEBUG */


/* 
- checks if there are any universally quantified variables left
- TODO: refinements
*/
static int
is_formula_existential (Nenofex * nenofex)
{
  int result = 1;

  void **v_scope, **scope_start;
  scope_start = nenofex->scopes->elems;
  for (v_scope = nenofex->scopes->top - 1;
       result && v_scope >= scope_start; v_scope--)
    {

      Scope *scope = *v_scope;
      if (is_universal_scope (scope))   /* check if all vars in scope eliminated */
        {
          void **v_var, **var_start;
          var_start = scope->vars->elems;
          for (v_var = scope->vars->top - 1; result && v_var >= var_start;
               v_var--)
            {
              Var *var = *v_var;
              result = (!variable_has_occs (var));
            }                   /* end: for all vars in scope */

          if (result)
            assert (is_empty_scope (nenofex, scope));
        }

    }                           /* end: for all scopes */

  return result;
}


/* 
- checks if there are any existentially quantified variables left
- TODO: refinements
*/
static int
is_formula_universal (Nenofex * nenofex)
{
  int result = 1;

  void **v_scope, **scope_start;
  scope_start = nenofex->scopes->elems;
  for (v_scope = nenofex->scopes->top - 1;
       result && v_scope >= scope_start; v_scope--)
    {

      Scope *scope = *v_scope;
      if (is_existential_scope (scope)) /* all vars in scope eliminated or have no occs left? */
        {
          void **v_var, **var_start;
          var_start = scope->vars->elems;
          for (v_var = scope->vars->top - 1; result && v_var >= var_start;
               v_var--)
            {
              Var *var = *v_var;
              result = (!variable_has_occs (var));
            }                   /* end: for all vars in scope */

          if (result)
            assert (is_empty_scope (nenofex, scope));
        }

    }                           /* end: for all scopes */

  return result;
}


/* 
- FOR DEBUGGING PURPOSES ONLY -> TODO: disable
- called before assigning IDs for tseitin transformation 
*/
static void
reset_all_node_ids (Nenofex * nenofex)
{
  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      cur->id = 0;
      if (!is_literal_node (cur))
        {
          Node *ch;
          for (ch = cur->child_list.last; ch; ch = ch->level_link.prev)
            push_stack (stack, ch);
        }
    }

  delete_stack (stack);
}


/* NNF-TO-CNF CONVERSION */

/* ------- START: STANDARD TSEITIN TRANSFORMATION ------- */

static void
nnf_to_cnf_standard_tseitin_assign_node_ids (Nenofex * nenofex)
{
  reset_all_node_ids (nenofex);

  nenofex->tseitin_next_id = 1;

  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;
      void **v_var, **var_end;
      var_end = scope->vars->top;
      for (v_var = scope->vars->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;
          if (variable_has_occs (var))
            {
              assert (!var->eliminated);
              var->id = nenofex->tseitin_next_id++;
            }
        }                       /* end: for all vars in scope */
    }                           /* end: for all scopes */

  nenofex->tseitin_first_op_node_id = nenofex->tseitin_next_id;

  /* traverse graph and assign IDs */

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (is_literal_node (cur))
        {
          Lit *lit = cur->lit;
          assert (lit->var->id > 0);
          assert (lit->var->id < nenofex->tseitin_first_op_node_id);
          cur->id = (lit->negated ? -lit->var->id : lit->var->id);
        }
      else
        {
          cur->id = nenofex->tseitin_next_id++;

          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);
}


static int
nnf_to_cnf_standard_tseitin_count_clauses (Nenofex * nenofex, int qnnf)
{
  if (!nenofex->graph_root)
    return 0;

  if (is_literal_node (nenofex->graph_root))
    {
      return 1;
    }

  int clause_cnt = 1;           /* count output at root */

  if (qnnf)
    clause_cnt = 0;

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (!is_literal_node (cur))
        {
          clause_cnt += cur->num_children + 1;

          Node *child;
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);

  return clause_cnt;
}


/* 
- assumes that IDs have been assigned to nodes
- dumps CNF to 'out'
*/
static void
nnf_to_cnf_standard_tseitin_dump (Nenofex * nenofex, FILE * out, int qnnf)
{
  assert (nenofex->graph_root);
  assert (qnnf || !is_formula_existential (nenofex)
          || !nenofex->sat_solver_tautology_mode);
  assert (qnnf || nenofex->sat_solver_tautology_mode
          || is_formula_existential (nenofex));

  assert (qnnf || !is_formula_universal (nenofex)
          || nenofex->sat_solver_tautology_mode);
  assert (qnnf || !nenofex->sat_solver_tautology_mode
          || is_formula_universal (nenofex));

  int num_clauses = nnf_to_cnf_standard_tseitin_count_clauses (nenofex, qnnf);
  fprintf (out, "p cnf %d %d\n", nenofex->tseitin_next_id - 1, num_clauses);

  if (qnnf)
    {
      /* Print prefix where tseitin variables occur in innermost
         (existential) scope. */
      void **v_scope, **scope_end;
      scope_end = nenofex->scopes->top;
      for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
        {
          Scope *scope = *v_scope;
          fprintf (out, "%c ", is_existential_scope (scope) ? 'e' : 'a');
          void **v_var, **var_end;
          var_end = scope->vars->top;
          for (v_var = scope->vars->elems; v_var < var_end; v_var++)
            {
              Var *var = *v_var;
              if (variable_has_occs (var))
                {
                  assert (!var->eliminated);
                  assert (var->id);
                  fprintf (out, "%d ", var->id);
                }
            }                   /* end: for all vars in scope */

          /* Must print tseitin variables in innermost scope. */
          if (v_scope == scope_end - 1)
            {
              fprintf (stderr, "Dump CNF: first,last tseitin id: %d,%d\n",
                       nenofex->tseitin_first_op_node_id,
                       nenofex->tseitin_next_id - 1);

              if (!is_existential_scope (scope))
                {
                  /* Could have innermost universal scope after
                     expansions. First close that scope and open new
                     existential one. */
                  fprintf (out, "0\n");
                  fprintf (out, "e ");
                }
              unsigned int tvar, tvar_end;
              for (tvar = nenofex->tseitin_first_op_node_id,
                   tvar_end = nenofex->tseitin_next_id; tvar < tvar_end;
                   tvar++)
                fprintf (out, "%d ", tvar);
            }

          fprintf (out, "0\n");
        }                       /* end: for all scopes */
    }

  assert (nenofex->graph_root->id);

  if (!qnnf)
    {
      int output = nenofex->sat_solver_tautology_mode ?
        -nenofex->graph_root->id : nenofex->graph_root->id;
      fprintf (out, "%d 0\n", output);
    }

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (is_and_node (cur))
        {
          assert (cur->id > 0);

          Node *child;
          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);
              fprintf (out, "%d %d 0\n", -cur->id, child->id);
            }

          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);
              fprintf (out, "%d ", -child->id);
            }
          fprintf (out, "%d 0\n", cur->id);

          /* TODO: pushing children could be done in loop before */
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
      else if (is_or_node (cur))
        {
          assert (cur->id > 0);

          Node *child;
          fprintf (out, "%d ", -cur->id);
          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);
              fprintf (out, "%d ", child->id);
            }
          fprintf (out, "0\n");

          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);
              fprintf (out, "%d %d 0\n", cur->id, -child->id);
            }

          /* TODO: pushing children could be done in loop before */
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);
}


/* 
- assumes that IDs have been assigned to nodes
- same code like in dumping but CNF is forwarded to internal SAT solver
*/
static void
nnf_to_cnf_standard_tseitin_forward (Nenofex * nenofex)
{
  assert (nenofex->graph_root);

  assert (!is_formula_existential (nenofex)
          || !nenofex->sat_solver_tautology_mode);
  assert (nenofex->sat_solver_tautology_mode
          || is_formula_existential (nenofex));

  assert (!is_formula_universal (nenofex)
          || nenofex->sat_solver_tautology_mode);
  assert (!nenofex->sat_solver_tautology_mode
          || is_formula_universal (nenofex));

  assert (nenofex->graph_root->id);

  int output = nenofex->sat_solver_tautology_mode ?
    -nenofex->graph_root->id : nenofex->graph_root->id;
  sat_solver_add (output);
  sat_solver_add (0);

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (is_and_node (cur))
        {
          assert (cur->id > 0);

          Node *child;
          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);
              sat_solver_add (-cur->id);
              sat_solver_add (child->id);
              sat_solver_add (0);
            }

          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);
              sat_solver_add (-child->id);
            }
          sat_solver_add (cur->id);
          sat_solver_add (0);

          /* TODO: pushing children could be done in loop before */
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
      else if (is_or_node (cur))
        {
          assert (cur->id > 0);

          Node *child;
          sat_solver_add (-cur->id);
          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);
              sat_solver_add (child->id);
            }
          sat_solver_add (0);

          for (child = cur->child_list.first; child;
               child = child->level_link.next)
            {
              assert (child->id);

              sat_solver_add (cur->id);
              sat_solver_add (-child->id);
              sat_solver_add (0);
            }

          /* TODO: pushing children could be done in loop before */
          for (child = cur->child_list.last; child;
               child = child->level_link.prev)
            push_stack (stack, child);
        }
    }                           /* end: while stack not empty */

  delete_stack (stack);
}

/* ------- END: STANDARD TSEITIN TRANSFORMATION ------- */


/* ------- START: REVISED TSEITIN TRANSFORMATION ------- */

static void
nnf_to_cnf_tseitin_revised_assign_node_ids (Nenofex * nenofex)
{
  reset_all_node_ids (nenofex);

  nenofex->tseitin_next_id = 1;

  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;
      void **v_var, **var_end;
      var_end = scope->vars->top;
      for (v_var = scope->vars->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;
          if (variable_has_occs (var))
            {
              assert (!var->eliminated);
              var->id = nenofex->tseitin_next_id++;
            }
        }                       /* end: for all vars in scope */
    }                           /* end: for all scopes */

  nenofex->tseitin_first_op_node_id = nenofex->tseitin_next_id;

  /* traverse graph and assign IDs */

  if (is_literal_node (nenofex->graph_root))
    {
      Lit *lit = nenofex->graph_root->lit;
      nenofex->graph_root->id = (lit->negated ? -lit->var->id : lit->var->id);
      return;
    }

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  if (is_and_node (nenofex->graph_root))
    {
      if (nenofex->sat_solver_tautology_mode)
        {
          Node *child;
          for (child = nenofex->graph_root->child_list.last;
               child; child = child->level_link.prev)
            {
              assert (!is_and_node (child));
              if (!is_literal_node (child))
                push_stack (stack, child);
              else
                {
                  Lit *lit = child->lit;
                  assert (lit->var->id > 0);
                  assert (lit->var->id < nenofex->tseitin_first_op_node_id);
                  child->id = (lit->negated ? -lit->var->id : lit->var->id);
                }
            }
        }
      else                      /* sat-mode */
        {
          Node *child;
          for (child = nenofex->graph_root->child_list.last;
               child; child = child->level_link.prev)
            {
              if (!is_literal_node (child))
                {
                  Node *child_child;
                  for (child_child = child->child_list.last;
                       child_child;
                       child_child = child_child->level_link.prev)
                    {
                      if (!is_literal_node (child_child))
                        {
                          push_stack (stack, child_child);
                        }
                      else
                        {
                          Lit *lit = child_child->lit;
                          assert (lit->var->id > 0);
                          assert (lit->var->id <
                                  nenofex->tseitin_first_op_node_id);
                          child_child->id =
                            (lit->negated ? -lit->var->id : lit->var->id);
                        }
                    }           /* end: for child's children */
                }
              else
                {
                  Lit *lit = child->lit;
                  assert (lit->var->id > 0);
                  assert (lit->var->id < nenofex->tseitin_first_op_node_id);
                  child->id = (lit->negated ? -lit->var->id : lit->var->id);
                }
            }                   /* end: for all children */
        }                       /* end: sat mode */
    }
  else                          /* OR */
    {
      if (!nenofex->sat_solver_tautology_mode)
        {
          Node *child;
          for (child = nenofex->graph_root->child_list.last;
               child; child = child->level_link.prev)
            {
              assert (!is_or_node (child));
              if (!is_literal_node (child))
                push_stack (stack, child);
              else
                {
                  Lit *lit = child->lit;
                  assert (lit->var->id > 0);
                  assert (lit->var->id < nenofex->tseitin_first_op_node_id);
                  child->id = (lit->negated ? -lit->var->id : lit->var->id);
                }
            }
        }
      else                      /* sat mode */
        {
          Node *child;
          for (child = nenofex->graph_root->child_list.last;
               child; child = child->level_link.prev)
            {
              if (!is_literal_node (child))
                {
                  Node *child_child;
                  for (child_child = child->child_list.last;
                       child_child;
                       child_child = child_child->level_link.prev)
                    {
                      if (!is_literal_node (child_child))
                        {
                          push_stack (stack, child_child);
                        }
                      else
                        {
                          Lit *lit = child_child->lit;
                          assert (lit->var->id > 0);
                          assert (lit->var->id <
                                  nenofex->tseitin_first_op_node_id);
                          child_child->id =
                            (lit->negated ? -lit->var->id : lit->var->id);
                        }
                    }           /* end: for child's children */
                }
              else
                {
                  Lit *lit = child->lit;
                  assert (lit->var->id > 0);
                  assert (lit->var->id < nenofex->tseitin_first_op_node_id);
                  child->id = (lit->negated ? -lit->var->id : lit->var->id);
                }
            }                   /* end: for all children */
        }                       /* end: sat mode */
    }                           /* end: OR */

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (nenofex->sat_solver_tautology_mode || is_and_node (cur));
      assert (!is_and_node (cur) || !nenofex->sat_solver_tautology_mode);

      assert (!nenofex->sat_solver_tautology_mode || is_or_node (cur));
      assert (!is_or_node (cur) || nenofex->sat_solver_tautology_mode);

      cur->id = nenofex->tseitin_next_id++;

      Node *child;
      for (child = cur->child_list.last; child;
           child = child->level_link.prev)
        {
          if (!is_literal_node (child))
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  if (!is_literal_node (child_child))
                    {
                      push_stack (stack, child_child);
                    }
                  else
                    {
                      Lit *lit = child_child->lit;
                      assert (lit->var->id > 0);
                      assert (lit->var->id <
                              nenofex->tseitin_first_op_node_id);
                      child_child->id =
                        (lit->negated ? -lit->var->id : lit->var->id);
                    }
                }               /* end: for child's children */
            }
          else
            {
              Lit *lit = child->lit;
              assert (lit->var->id > 0);
              assert (lit->var->id < nenofex->tseitin_first_op_node_id);
              child->id = (lit->negated ? -lit->var->id : lit->var->id);
            }
        }                       /* end: for all children */

    }                           /* end: while stack not empty */

  delete_stack (stack);
}


static int
nnf_to_cnf_tseitin_revised_count_clauses (Nenofex * nenofex)
{
  if (!nenofex->graph_root)
    return 0;

#ifndef NDEBUG
  if (nenofex->cur_expansions != 0)
    {
      assert (!is_formula_existential (nenofex)
              || !nenofex->sat_solver_tautology_mode);
      assert (nenofex->sat_solver_tautology_mode
              || is_formula_existential (nenofex));

      assert (!is_formula_universal (nenofex)
              || nenofex->sat_solver_tautology_mode);
      assert (!nenofex->sat_solver_tautology_mode
              || is_formula_universal (nenofex));
    }
#endif

  if (is_literal_node (nenofex->graph_root))
    {
      return 1;
    }

  int clause_cnt = 0;

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  if (is_and_node (nenofex->graph_root))
    {
      if (nenofex->sat_solver_tautology_mode)
        {
          clause_cnt++;         /* one clause for top AND */
          Node *child;
          for (child = nenofex->graph_root->child_list.last;
               child; child = child->level_link.prev)
            {
              assert (!is_and_node (child));
              if (!is_literal_node (child))
                push_stack (stack, child);
            }
        }
      else
        push_stack (stack, nenofex->graph_root);
    }
  else                          /* OR */
    {
      if (!nenofex->sat_solver_tautology_mode)
        {
          clause_cnt++;         /* one clause for top-OR */
          Node *child;
          for (child = nenofex->graph_root->child_list.last;
               child; child = child->level_link.prev)
            {
              assert (!is_or_node (child));
              if (!is_literal_node (child))
                push_stack (stack, child);
            }
        }
      else
        push_stack (stack, nenofex->graph_root);
    }

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (nenofex->sat_solver_tautology_mode || is_and_node (cur));
      assert (!is_and_node (cur) || !nenofex->sat_solver_tautology_mode);

      assert (!nenofex->sat_solver_tautology_mode || is_or_node (cur));
      assert (!is_or_node (cur) || nenofex->sat_solver_tautology_mode);

      clause_cnt += cur->num_children;

      Node *child;
      for (child = cur->child_list.last; child;
           child = child->level_link.prev)
        {
          if (!is_literal_node (child))
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  if (!is_literal_node (child_child))
                    {
                      push_stack (stack, child_child);
                    }
                }               /* end: for child's children */
            }
        }                       /* end: for all children */

    }                           /* end: while stack not empty */

  delete_stack (stack);

  return clause_cnt;
}


/*
- sat mode
*/
static void
nnf_to_cnf_tseitin_revised_top_truth_dump (Nenofex * nenofex, FILE * out)
{
  assert (nenofex->graph_root);
  assert (is_formula_existential (nenofex));
  assert (!nenofex->sat_solver_tautology_mode);

  int num_clauses = nnf_to_cnf_tseitin_revised_count_clauses (nenofex);
  fprintf (out, "p cnf %d %d\n", nenofex->tseitin_next_id - 1, num_clauses);

  if (is_literal_node (nenofex->graph_root))
    {
      assert (nenofex->graph_root->id);
      fprintf (out, "%d 0\n", nenofex->graph_root->id);
      return;
    }

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  if (is_and_node (nenofex->graph_root))
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last;
           child; child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          if (is_literal_node (child))
            {
              assert (child->id);
              fprintf (out, "%d 0\n", child->id);
            }
          else                  /* OR */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  fprintf (out, "%d ", child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              fprintf (out, "0\n");
            }
        }                       /* end: for AND's children */
    }
  else                          /* root is OR */
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last;
           child; child = child->level_link.prev)
        {
          assert (child->id);
          fprintf (out, "%d ", child->id);      /* print top-or constraint */
          assert (!is_or_node (child));
          if (!is_literal_node (child))
            push_stack (stack, child);
        }
      fprintf (out, "0\n");
    }

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (is_and_node (cur));
      assert (cur->id);

      Node *child;
      for (child = cur->child_list.last; child;
           child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          fprintf (out, "%d ", -cur->id);
          if (is_literal_node (child))
            {
              assert (child->id);
              fprintf (out, "%d 0\n", child->id);
            }
          else                  /* OR */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  fprintf (out, "%d ", child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              fprintf (out, "0\n");
            }
        }                       /* end: for AND's children */

    }                           /* end: while stack not empty */

  delete_stack (stack);
}


/*
- same code as in dumping but now forward to internal sat solver
*/
static void
nnf_to_cnf_tseitin_revised_top_truth_forward (Nenofex * nenofex)
{
  assert (nenofex->graph_root);
  assert (is_formula_existential (nenofex));
  assert (!nenofex->sat_solver_tautology_mode);

  if (is_literal_node (nenofex->graph_root))
    {
      assert (nenofex->graph_root->id);
      sat_solver_add (nenofex->graph_root->id);
      sat_solver_add (0);
      return;
    }

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  if (is_and_node (nenofex->graph_root))
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last;
           child; child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          if (is_literal_node (child))
            {
              assert (child->id);
              sat_solver_add (child->id);
              sat_solver_add (0);
            }
          else                  /* OR */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  sat_solver_add (child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              sat_solver_add (0);
            }
        }                       /* end: for AND's children */
    }
  else                          /* root is OR */
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last;
           child; child = child->level_link.prev)
        {
          assert (child->id);
          sat_solver_add (child->id);
          assert (!is_or_node (child));
          if (!is_literal_node (child))
            push_stack (stack, child);
        }
      sat_solver_add (0);
    }

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (is_and_node (cur));
      assert (cur->id);

      Node *child;
      for (child = cur->child_list.last; child;
           child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          sat_solver_add (-cur->id);
          if (is_literal_node (child))
            {
              assert (child->id);
              sat_solver_add (child->id);
              sat_solver_add (0);
            }
          else                  /* OR */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  sat_solver_add (child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              sat_solver_add (0);
            }
        }                       /* end: for AND's children */

    }                           /* end: while stack not empty */

  delete_stack (stack);
}


/*
- tautology mode
*/
static void
nnf_to_cnf_tseitin_revised_top_falsity_dump (Nenofex * nenofex, FILE * out)
{
  assert (nenofex->graph_root);
  assert (is_formula_universal (nenofex));
  assert (nenofex->sat_solver_tautology_mode);

  int num_clauses = nnf_to_cnf_tseitin_revised_count_clauses (nenofex);
  fprintf (out, "p cnf %d %d\n", nenofex->tseitin_next_id - 1, num_clauses);

  if (is_literal_node (nenofex->graph_root))
    {
      assert (nenofex->graph_root->id);
      fprintf (out, "%d 0\n", -nenofex->graph_root->id);
      return;
    }

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  if (is_or_node (nenofex->graph_root))
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last;
           child; child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          if (is_literal_node (child))
            {
              assert (child->id);
              fprintf (out, "%d 0\n", -child->id);
            }
          else                  /* AND */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  fprintf (out, "%d ", -child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              fprintf (out, "0\n");
            }
        }                       /* end: for OR's children */
    }
  else                          /* root is AND */
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last;
           child; child = child->level_link.prev)
        {
          assert (child->id);
          fprintf (out, "%d ", -child->id);     /* print top-and constraint */
          assert (!is_and_node (child));
          if (!is_literal_node (child))
            push_stack (stack, child);
        }
      fprintf (out, "0\n");
    }

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (is_or_node (cur));
      assert (cur->id);

      Node *child;
      for (child = cur->child_list.last; child;
           child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          fprintf (out, "%d ", cur->id);
          if (is_literal_node (child))
            {
              assert (child->id);
              fprintf (out, "%d 0\n", -child->id);
            }
          else                  /* AND */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  fprintf (out, "%d ", -child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              fprintf (out, "0\n");
            }
        }                       /* end: for OR's children */

    }                           /* end: while stack not empty */

  delete_stack (stack);
}


/*
- same code as in dumping but now forward to internal sat solver
*/
static void
nnf_to_cnf_tseitin_revised_top_falsity_forward (Nenofex * nenofex)
{
  assert (nenofex->graph_root);
  assert (is_formula_universal (nenofex));
  assert (nenofex->sat_solver_tautology_mode);

  if (is_literal_node (nenofex->graph_root))
    {
      assert (nenofex->graph_root->id);
      sat_solver_add (-nenofex->graph_root->id);
      sat_solver_add (0);
      return;
    }

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);

  if (is_or_node (nenofex->graph_root))
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last; child;
           child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          if (is_literal_node (child))
            {
              assert (child->id);
              sat_solver_add (-child->id);
              sat_solver_add (0);
            }
          else                  /* AND */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  sat_solver_add (-child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              sat_solver_add (0);
            }
        }                       /* end: for AND's children */
    }
  else                          /* root is AND */
    {
      Node *child;
      for (child = nenofex->graph_root->child_list.last;
           child; child = child->level_link.prev)
        {
          assert (child->id);
          sat_solver_add (-child->id);
          assert (!is_and_node (child));
          if (!is_literal_node (child))
            push_stack (stack, child);
        }
      sat_solver_add (0);
    }

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      assert (is_or_node (cur));
      assert (cur->id);

      Node *child;
      for (child = cur->child_list.last; child;
           child = child->level_link.prev)
        {                       /* each 'child' generates a clause where child's children make up set of literals */
          sat_solver_add (cur->id);
          if (is_literal_node (child))
            {
              assert (child->id);
              sat_solver_add (-child->id);
              sat_solver_add (0);
            }
          else                  /* AND */
            {
              Node *child_child;
              for (child_child = child->child_list.last;
                   child_child; child_child = child_child->level_link.prev)
                {
                  assert (child_child->id);
                  sat_solver_add (-child_child->id);
                  if (!is_literal_node (child_child))
                    push_stack (stack, child_child);
                }               /* end: for all child's children */
              sat_solver_add (0);
            }
        }                       /* end: for AND's children */

    }                           /* end: while stack not empty */

  delete_stack (stack);
}

/* ------- END: REVISED TSEITIN TRANSFORMATION ------- */


static int count_variables (Nenofex * nenofex, const int count_existential);


static void
quantified_nnf_to_cnf_dump (Nenofex * nenofex, FILE * out)
{
  int num_clauses = 0, num_vars = 0, num_remaining_vars = 0,
    num_remaining_exist_vars = 0, num_remaining_univ_vars = 0;

  /* Use standard-tseitin encoding. */
  nnf_to_cnf_standard_tseitin_assign_node_ids (nenofex);
  num_clauses = nnf_to_cnf_standard_tseitin_count_clauses (nenofex, 1);
  num_vars = nenofex->tseitin_next_id - 1;

  fprintf (stderr, "\nDumped CNF:\n");
  fprintf (stderr, "  tseitin variables: %d\n", num_vars);
  fprintf (stderr, "  tseitin clauses: %d\n", num_clauses);
  nnf_to_cnf_standard_tseitin_dump (nenofex, out, 1);
}


static void
nnf_to_cnf_dump (Nenofex * nenofex, FILE * out)
{
  int num_clauses = 0, num_vars = 0, num_remaining_vars = 0,
    num_remaining_exist_vars = 0, num_remaining_univ_vars = 0;

  if (nenofex->options.cnf_generator_tseitin_revised_specified)
    {
      nnf_to_cnf_tseitin_revised_assign_node_ids (nenofex);
      num_clauses = nnf_to_cnf_tseitin_revised_count_clauses (nenofex);
    }
  else
    {
      nnf_to_cnf_standard_tseitin_assign_node_ids (nenofex);
      num_clauses = nnf_to_cnf_standard_tseitin_count_clauses (nenofex, 0);
    }

  num_vars = nenofex->tseitin_next_id - 1;
  num_remaining_exist_vars = count_variables (nenofex, 1);
  num_remaining_univ_vars = count_variables (nenofex, 0);
  num_remaining_vars = num_remaining_exist_vars + num_remaining_univ_vars;

  fprintf (stderr, "\nDumped CNF:\n");
  fprintf (stderr, "  tseitin variables: %d\n", num_vars);
  fprintf (stderr, "  tseitin clauses: %d\n", num_clauses);

  if (nenofex->sat_solver_tautology_mode)
    {
      if (nenofex->options.cnf_generator_tseitin_revised_specified)
        nnf_to_cnf_tseitin_revised_top_falsity_dump (nenofex, out);
      else
        nnf_to_cnf_standard_tseitin_dump (nenofex, out, 0);
    }
  else
    {
      if (nenofex->options.cnf_generator_tseitin_revised_specified)
        nnf_to_cnf_tseitin_revised_top_truth_dump (nenofex, out);
      else
        nnf_to_cnf_standard_tseitin_dump (nenofex, out, 0);
    }
}


static void
nnf_to_cnf_forward (Nenofex * nenofex)
{
  int num_clauses = 0, num_vars = 0, num_remaining_vars = 0,
    num_remaining_exist_vars = 0, num_remaining_univ_vars = 0;

  if (nenofex->options.cnf_generator_tseitin_revised_specified)
    {
      nnf_to_cnf_tseitin_revised_assign_node_ids (nenofex);
      num_clauses = nnf_to_cnf_tseitin_revised_count_clauses (nenofex);
    }
  else
    {
      nnf_to_cnf_standard_tseitin_assign_node_ids (nenofex);
      num_clauses = nnf_to_cnf_standard_tseitin_count_clauses (nenofex, 0);
    }

  num_vars = nenofex->tseitin_next_id - 1;
  num_remaining_exist_vars = count_variables (nenofex, 1);
  num_remaining_univ_vars = count_variables (nenofex, 0);
  num_remaining_vars = num_remaining_exist_vars + num_remaining_univ_vars;

  if (!nenofex->options.print_short_answer_specified)
    {
      fprintf (stderr, "\nForwarded CNF:\n");
      fprintf (stderr, "  tseitin variables: %d\n", num_vars);
      fprintf (stderr, "  tseitin clauses: %d\n", num_clauses);
    }

  if (nenofex->sat_solver_tautology_mode)
    {
      if (nenofex->options.cnf_generator_tseitin_revised_specified)
        nnf_to_cnf_tseitin_revised_top_falsity_forward (nenofex);
      else
        nnf_to_cnf_standard_tseitin_forward (nenofex);
    }
  else
    {
      if (nenofex->options.cnf_generator_tseitin_revised_specified)
        nnf_to_cnf_tseitin_revised_top_truth_forward (nenofex);
      else
        nnf_to_cnf_standard_tseitin_forward (nenofex);
    }
}


/*
- does NOT yet work properly 
- TODO: REVISION - currently variables' IDs are all reassigned from scratch
*/
static void
get_var_assignments_from_sat_solver (Nenofex * nenofex)
{

#ifndef NDEBUG
  do
    {
      void **v_isp = nenofex->scopes->elems;
      Scope *scope = *v_isp;
      assert (scope->nesting == DEFAULT_SCOPE_NESTING);
      if (count_stack (scope->vars) == 0)       /* QBF instance */
        {
          v_isp++;
          scope = *v_isp;
          assert (scope->nesting == 1);
        }

      if (is_universal_scope (scope))
        assert (nenofex->result == NENOFEX_RESULT_UNSAT);
      else
        assert (nenofex->result == NENOFEX_RESULT_SAT);
    }
  while (0);
#endif

  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;
      void **v_var, **var_end;
      var_end = scope->vars->top;
      for (v_var = scope->vars->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;
          if (variable_has_occs (var))
            {
              assert (!var->eliminated);
              assert (var->id > 0);

              int sat_solver_deref = sat_solver_deref (var->id);
              VarAssignment assignment;
              if (sat_solver_deref > 0)
                assignment = VAR_ASSIGNMENT_TRUE;
              else if (sat_solver_deref < 0)
                assignment = VAR_ASSIGNMENT_FALSE;
              else
                assignment = VAR_ASSIGNMENT_UNDEFINED;
              var->assignment = assignment;
            }
        }                       /* end: for all vars in scope */
    }                           /* end: for all scopes */
}


/*
- does NOT yet work properly 
- TODO: REVISION - currently variables' IDs are all reassigned from scratch
*/
static void
generate_qdimacs_output (Nenofex * nenofex, FILE * out)
{
  assert (nenofex->result != NENOFEX_RESULT_UNKNOWN);

  int answer;
  if (nenofex->result == NENOFEX_RESULT_SAT)
    answer = 1;
  else if (nenofex->result == NENOFEX_RESULT_UNSAT)
    answer = 0;
  else
    answer = -1;

  fprintf (out, "s cnf %d %d %d\n", answer, nenofex->num_orig_vars,
           nenofex->num_orig_clauses);

  void **v_isp = nenofex->scopes->elems;
  Scope *scope = *v_isp;
  assert (scope->nesting == DEFAULT_SCOPE_NESTING);
  if (count_stack (scope->vars) == 0)   /* QBF instance */
    {
      v_isp++;
      scope = *v_isp;
      assert (scope->nesting == 1);
    }

  void **v_var, **var_end;
  var_end = scope->vars->top;
  for (v_var = scope->vars->elems; v_var < var_end; v_var++)
    {
      Var *var = *v_var;
      assert (var->id > 0);
      int value = var->assignment - 1;
      assert (var->assignment != VAR_ASSIGNMENT_UNDEFINED || value == -1);
      assert (var->assignment != VAR_ASSIGNMENT_FALSE || value == 0);
      assert (var->assignment != VAR_ASSIGNMENT_TRUE || value == 1);

      /* likely to fail.. */
      assert (var->assignment != VAR_ASSIGNMENT_UNDEFINED);

      int lit = var->id;
      if (var->assignment == VAR_ASSIGNMENT_FALSE)
        lit = -lit;
      fprintf (out, "V %d \n", lit);
    }
}


static int
is_unsigned_string (char *str)
{
  int result;
  char *p = str;

  result = p && *p;
  while (p && *p && (result = isdigit (*p++)))
    ;

  return result;
}


static void
set_default_cmd_line_options (Nenofex * nenofex)
{
  nenofex->options.cnf_generator_tseitin_revised_specified = 1;
  nenofex->options.cnf_generator_tseitin_specified = 0;

  nenofex->options.opt_subgraph_limit_specified = 1;
  nenofex->options.opt_subgraph_limit = 500;

  nenofex->options.univ_trigger = 10;
  nenofex->options.univ_trigger_delta = 10;

  nenofex->options.print_short_answer_specified = 1;
}


#ifndef NDEBUG
/*
- for assertion checking only
*/
static void
assert_solver_options (Nenofex * nenofex)
{
  assert (!nenofex->options.num_expansions_specified ||
          nenofex->options.num_expansions > 0);
  assert (nenofex->options.num_expansions <= 0 ||
          nenofex->options.num_expansions_specified);

  assert (!nenofex->options.cnf_generator_tseitin_specified ||
          !nenofex->options.cnf_generator_tseitin_revised_specified);
  assert (nenofex->options.cnf_generator_tseitin_revised_specified ||
          nenofex->options.cnf_generator_tseitin_specified);

  assert (!nenofex->options.size_cutoff_absolute_specified ||
          !nenofex->options.size_cutoff_relative_specified);
}
#endif


/*
- parse and set command line options
*/
static int
parse_cmd_line_options (Nenofex * nenofex, int argc, char **argv)
{
  int done = 0;

  int opt_cnt;
  for (opt_cnt = 1; opt_cnt < argc; opt_cnt++)
    {
      char *opt_str = argv[opt_cnt];

      if (!strcmp (opt_str, "-n"))
        {
          nenofex->options.num_expansions_specified = 1;

          char *num_str = argv[++opt_cnt];
          if (is_unsigned_string (num_str))
            {
              nenofex->options.num_expansions = atoi (num_str);
              if (nenofex->options.num_expansions == 0)
                {
                  fprintf (stderr, "Expecting value > 0 after '-n'\n\n");
                  exit (1);
                }
            }
          else
            {
              fprintf (stderr,
                       "Expecting non-zero positive integer after '-n'\n\n");
              exit (1);
            }
        }
      else
        if (!strncmp (opt_str, "--size-cutoff=", strlen ("--size-cutoff=")))
        {
          opt_str += strlen ("--size-cutoff=");

          if (strlen (opt_str) == 0)
            {
              fprintf (stderr, "Expecting value after '--size-cutoff='\n\n");
              exit (1);
            }

          double size_cutoff = atof (opt_str);

          double integer = 0;
          double fractional = modf (size_cutoff, &integer);

          if (fractional)
            {                   /* relative size cutoff */
              if ((size_cutoff <= -1.0F) || (size_cutoff >= 1.0F))
                {
                  fprintf (stderr,
                           "Expecting '-1.0 < size_cutoff < 1.0' or an integer\n\n");
                  exit (1);
                }

              nenofex->options.size_cutoff_relative_specified = 1;
            }
          else                  /* absolute size cutoff */
            {
              nenofex->options.size_cutoff_absolute_specified = 1;
            }

          nenofex->options.size_cutoff = size_cutoff;
        }
      else
        if (!strncmp (opt_str, "--cost-cutoff=", strlen ("--cost-cutoff=")))
        {
          opt_str += strlen ("--cost-cutoff=");

          if (strlen (opt_str) == 0)
            {
              fprintf (stderr, "Expecting value after '--cost-cutoff='\n\n");
              exit (1);
            }

          nenofex->options.cost_cutoff_specified = 1;
          nenofex->options.cost_cutoff = atoi (opt_str);
        }
      else
        if (!strncmp
            (opt_str, "--propagation-limit=",
             strlen ("--propagation-limit=")))
        {
          opt_str += strlen ("--propagation-limit=");

          if (strlen (opt_str) == 0)
            {
              fprintf (stderr,
                       "Expecting value after '--propagation-limit='\n\n");
              exit (1);
            }

          nenofex->options.propagation_limit_specified = 1;

          if (is_unsigned_string (opt_str))
            {
              nenofex->options.propagation_limit = atoi (opt_str);
            }
          else
            {
              fprintf (stderr,
                       "Expecting positive integer after '--propagation-limit='\n\n");
              exit (1);
            }
        }
      else
        if (!strncmp (opt_str, "--univ-trigger=", strlen ("--univ-trigger=")))
        {
          opt_str += strlen ("--univ-trigger=");

          if (strlen (opt_str) == 0)
            {
              fprintf (stderr,
                       "Expecting value or 'abs' after '--univ-trigger='\n\n");
              exit (1);
            }

          if (is_unsigned_string (opt_str))
            {
              nenofex->options.univ_trigger_abs = 0;
              nenofex->options.univ_trigger = atoi (opt_str);
            }
          else
            {
              if (!strcmp (opt_str, "abs"))
                nenofex->options.univ_trigger_abs = 1;
              else
                {
                  fprintf (stderr,
                           "Expecting positive integer or 'abs' after '--univ-trigger='\n\n");
                  exit (1);
                }
            }
        }
      else if (!strncmp (opt_str, "--univ-delta=", strlen ("--univ-delta=")))
        {
          opt_str += strlen ("--univ-delta=");

          if (strlen (opt_str) == 0)
            {
              fprintf (stderr, "Expecting value after '--univ-delta='\n\n");
              exit (1);
            }

          if (is_unsigned_string (opt_str))
            {
              nenofex->options.univ_trigger_delta = atoi (opt_str);
            }
          else
            {
              fprintf (stderr,
                       "Expecting positive integer after '--univ-delta='\n\n");
              exit (1);
            }
        }
      else
        if (!strncmp
            (opt_str, "--opt-subgraph-limit=",
             strlen ("--opt-subgraph-limit=")))
        {
          opt_str += strlen ("--opt-subgraph-limit=");

          if (strlen (opt_str) == 0)
            {
              fprintf (stderr,
                       "Expecting value after '--opt-subgraph-limit='\n\n");
              exit (1);
            }

          if (is_unsigned_string (opt_str))
            {
              nenofex->options.opt_subgraph_limit = atoi (opt_str);
              if (nenofex->options.opt_subgraph_limit == 0)
                {
                  fprintf (stderr,
                           "Expecting value > 0 after '--opt-subgraph-limit='\n\n");
                  exit (1);
                }
            }
          else
            {
              fprintf (stderr,
                       "Expecting non-zero positive integer after '--opt-subgraph-limit='\n\n");
              exit (1);
            }

          nenofex->options.opt_subgraph_limit_specified = 1;
        }
      else if (!strcmp (opt_str, "-h") || !strcmp (opt_str, "--help"))
        {
          done = 1;
          fprintf (stdout, USAGE);
        }

      else if (!strcmp (opt_str, "--version"))
        {
          done = 1;
          fprintf (stdout, VERSION);
        }

      else if (!strcmp (opt_str, "--verbose-sat-solving"))
        {
          nenofex->options.verbose_sat_solving_specified = 1;
        }
      else if (!strcmp (opt_str, "-v"))
        {
          nenofex->options.print_short_answer_specified = 0;
        }
      else if (!strcmp (opt_str, "--no-optimizations"))
        {
          nenofex->options.no_optimizations_specified = 1;
        }
      else if (!strcmp (opt_str, "--post-expansion-flattening"))
        {
          nenofex->options.post_expansion_flattening_specified = 1;
        }
      else if (!strcmp (opt_str, "--no-atpg"))
        {
          nenofex->options.no_atpg_specified = 1;
        }
      else if (!strcmp (opt_str, "--no-global-flow"))
        {
          nenofex->options.no_global_flow_specified = 1;
        }
      else if (!strcmp (opt_str, "--full-expansion"))
        {
          nenofex->options.full_expansion_specified = 1;
        }
      else if (!strcmp (opt_str, "--dump-cnf"))
        {
          nenofex->options.dump_cnf_specified = 1;
        }
      else if (!strcmp (opt_str, "--no-sat-solving"))
        {
          nenofex->options.no_sat_solving_specified = 1;
        }
      else if (!strcmp (opt_str, "--show-progress"))
        {
          nenofex->options.show_progress_specified = 1;
        }
      else if (!strcmp (opt_str, "--show-opt-info"))
        {
          nenofex->options.show_opt_info_specified = 1;
        }
      else if (!strcmp (opt_str, "--show-graph-size"))
        {
          nenofex->options.show_graph_size_specified = 1;
        }
#if 0                           /* does not yet work */
      else if (!strcmp (opt_str, "--print-assignment"))
        {
          nenofex->options.print_assignment_specified = 1;
        }
#endif
      else
        if (!strncmp
            (opt_str, "--cnf-generator=", strlen ("--cnf-generator=")))
        {
          opt_str += strlen ("--cnf-generator=");
          if (!strcmp (opt_str, "tseitin"))
            {
              nenofex->options.cnf_generator_tseitin_specified = 1;
              nenofex->options.cnf_generator_tseitin_revised_specified = 0;
            }
          else if (!strcmp (opt_str, "tseitin_revised"))
            {
              nenofex->options.cnf_generator_tseitin_revised_specified = 1;
              nenofex->options.cnf_generator_tseitin_specified = 0;
            }
          else
            {
              fprintf (stderr, "Unknown option %s\n", argv[opt_cnt]);
              exit (1);
            }
        }
      else if (is_unsigned_string (opt_str))
        {
          nenofex->options.max_time = atoi (opt_str);
          if (nenofex->options.max_time == 0)
            {
              fprintf (stderr, "Expecting value > 0 for max-time limit\n\n");
              exit (1);
            }
        }
      else if (!strncmp (opt_str, "-", 1) || !strncmp (opt_str, "--", 2))
        {
          fprintf (stderr, "Unknown option %s\n", opt_str);
          exit (1);
        }
      else if (!nenofex->options.input_filename)
        {
          nenofex->options.input_filename = opt_str;
        }
      else
        {
          fprintf (stderr, "Unknown option %s\n", opt_str);
          exit (1);
        }

    }                           /* end: for all arguments */

  return done;
}


/*
- traverse LCA of unit variable and mark all occurring variables 
*/
static void
simplify_mark_lca_variables_for_update (Nenofex * nenofex, Var * var,
                                        Stack * node_stack)
{
  assert (!count_stack (node_stack));

  Node **ch, *child;
  for (ch = var->exp_costs.lca_object.children; (child = *ch); ch++)
    push_stack (node_stack, child);

  Node *node;
  while ((node = pop_stack (node_stack)))
    {
      if (is_literal_node (node))
        {                       /* mark variable */
          Var *var = node->lit->var;

          lca_update_mark (var);
          inc_score_update_mark (var);
          dec_score_update_mark (var);
          collect_variable_for_update (nenofex, var);
        }
      else                      /* visit children */
        {
          for (child = node->child_list.first; child;
               child = child->level_link.next)
            push_stack (node_stack, child);
        }
    }                           /* end: while stack not empty */
}


/* 
- propagate unate variable by setting value accordingly 
*/
static void
simplify_eliminate_unate (Nenofex * nenofex, Var * var)
{
  assert (variable_has_occs (var));

#if COMPUTE_NUM_UNATES
  nenofex->stats.num_unates++;
#endif

#ifndef NDEBUG
  if (!var->lits[0].occ_list.first)     /* has no neg. occurrences */
    {
      assert (!var->lits[0].occ_list.last);
      assert (var->lits[1].occ_list.first);
      assert (var->lits[1].occ_list.last);
    }
  else                          /* has no pos. occurrences */
    {
      assert (!var->lits[1].occ_list.first);
      assert (!var->lits[1].occ_list.last);
      assert (var->lits[0].occ_list.first);
      assert (var->lits[0].occ_list.last);
    }
#endif

  Lit *lit = !var->lits[0].occ_list.first ? var->lits + 1 : var->lits;
  Node *occ;

  if (is_existential_scope (var->scope))
    {
      while ((occ = lit->occ_list.first))
        {
          propagate_truth (nenofex, occ);
        }
    }
  else                          /* universal variable */
    {
      while ((occ = lit->occ_list.first))
        {
          propagate_falsity (nenofex, occ);
        }
    }

  assert (!variable_has_occs (var));
}


/* 
- search and eliminate unates by checking candidate variables on stack 
- (might also contain variables which have no more occurrences left at all)
*/
static int
simplify_eliminate_unates (Nenofex * nenofex)
{
  Stack *node_stack = 0;

  int found = 0;

  Var *var;
  while ((var = pop_stack (nenofex->unates)))
    {
      assert (var->collected_as_unate);
      var->collected_as_unate = 0;

      if (!variable_has_occs (var))
        {
          var->scope->remaining_var_cnt--;
          assert (var->scope->remaining_var_cnt >= 0);

          if (var->scope->remaining_var_cnt == 0)
            var->scope->is_empty = 1;

          if ((var)->exp_costs.lca_object.lca)
            {
              unlink_variable_from_lca_list (var);
              reset_lca_object (nenofex, var, &(var)->exp_costs.lca_object,
                                1);
            }

          if (var->priority_pos != -1)
            {
              delete_elem_priority_queue (var->scope->priority_heap,
                                          var->priority_pos);
            }

          continue;
        }                       /* end: variable has no occs left */

      assert (var->lits[0].occ_list.first || var->lits[0].occ_cnt == 0);
      assert (var->lits[1].occ_list.first || var->lits[1].occ_cnt == 0);

      Lit *lit = var->lits;
      if (!lit->occ_list.first || !(++lit)->occ_list.first)
        {
          assert (var->lits[0].occ_list.first || !var->lits[0].occ_list.last);
          assert (var->lits[1].occ_list.first || !var->lits[1].occ_list.last);

          if (!node_stack)
            node_stack = create_stack (DEFAULT_STACK_SIZE);

          found++;

          if (!var->exp_costs.lca_object.lca || cost_update_marked (var))
            {                   /* re-initialize variable's LCA */
              if ((var)->exp_costs.lca_object.lca)
                {
                  unlink_variable_from_lca_list (var);
                  reset_lca_object (nenofex, var,
                                    &(var)->exp_costs.lca_object, 1);
                }
              find_variable_lca_and_children (nenofex, var,
                                              &var->exp_costs.lca_object, 1);
            }

          assign_or_update_changed_subformula (&(var->exp_costs.lca_object));
          mark_affected_scope_variables_for_cost_update (nenofex,
                                                         var->exp_costs.
                                                         lca_object.lca);
          simplify_mark_lca_variables_for_update (nenofex, var, node_stack);
          simplify_eliminate_unate (nenofex, var);
        }                       /* end: found unate variable */

    }                           /* end: while stack not empty */

  if (node_stack)
    {
      delete_stack (node_stack);
      node_stack = 0;
    }

  assert (!node_stack);

  return found;
}


/* 
- immediately conclude falsity
*/
static void
simplify_universal_unit (Nenofex * nenofex)
{
  remove_and_free_subformula (nenofex, nenofex->graph_root);
  assert (nenofex->graph_root == 0);

  nenofex->result = NENOFEX_RESULT_UNSAT;
}


/*
- check root-AND if it has any literal children
- can be done efficiently since by convention lits are stored first in child-list
- NOTE: can check for OR-root as well
*/
static int
simplify_eliminate_units (Nenofex * nenofex)
{
  Node *nenofex_graph_root = nenofex->graph_root;
  Stack *node_stack = 0;
  int found = 0;

  if (!nenofex_graph_root)
    return found;

  if (!is_and_node (nenofex_graph_root))
    return found;

  unsigned int exist_cnt;

  exist_cnt = 0;

  Node *literal = nenofex_graph_root->child_list.first;
  assert (literal);

  if (is_literal_node (literal))
    node_stack = create_stack (DEFAULT_STACK_SIZE);

  while (literal && is_literal_node (literal))
    {
#if COMPUTE_NUM_UNITS
      nenofex->stats.num_units++;
#endif

      found = 1;
      Lit *literal_lit = literal->lit;
      Var *var = literal_lit->var;
      Lit *lit = var->lits;

      if (!var->exp_costs.lca_object.lca || cost_update_marked (var))
        {                       /* re-initialize variable's LCA */
          if ((var)->exp_costs.lca_object.lca)
            {
              unlink_variable_from_lca_list (var);
              reset_lca_object (nenofex, var, &(var)->exp_costs.lca_object,
                                1);
            }
          find_variable_lca_and_children (nenofex, var,
                                          &var->exp_costs.lca_object, 1);
        }

      simplify_mark_lca_variables_for_update (nenofex, var, node_stack);

      assign_or_update_changed_subformula (&(var->exp_costs.lca_object));

      if (is_existential_scope (var->scope))
        {
          exist_cnt++;

          if (literal_lit->negated)
            {                   /* eliminate negative unit literal -> set variable to false */
              assert (lit->negated);
              Node *occ;

              while ((occ = lit->occ_list.first))
                {
                  propagate_truth (nenofex, occ);
                }

              lit++;
              assert (!lit->negated);
              while ((occ = lit->occ_list.first))
                {
                  propagate_falsity (nenofex, occ);
                }

              assert (!variable_has_occs (var));
            }
          else
            {                   /* eliminate positive unit literal -> set variable to true */
              assert (lit->negated);
              Node *occ;

              while ((occ = lit->occ_list.first))
                {
                  propagate_falsity (nenofex, occ);
                }

              lit++;
              assert (!lit->negated);
              while ((occ = lit->occ_list.first))
                {
                  propagate_truth (nenofex, occ);
                }

              assert (!variable_has_occs (var));
            }
        }
      else                      /* universal unit literal */
        {
          if (nenofex->options.show_progress_specified)
            fprintf (stderr, "Found univ. unit literal at root-AND\n");
          simplify_universal_unit (nenofex);
          break;
        }

      literal = (nenofex_graph_root = nenofex->graph_root)
        && is_and_node (nenofex_graph_root) ? nenofex_graph_root->child_list.
        first : 0;
    }                           /* end: while unit literals present */

  if (nenofex->options.show_progress_specified && exist_cnt)
    fprintf (stderr, "Found %d exist. unit literals at root-AND\n",
             exist_cnt);

  if (node_stack)
    {
      delete_stack (node_stack);
    }

  return found;
}


/*
- 'changed_subgraph' will become larger if expansions become more costly
- ATPG optimization will have to work on large graph and become VERY expensive
- idea: restrict graph size -> can then be optimized until saturation
- traverses subgraph until child of adequate size found
- collect siblings of this child until size limit reached
*/
static void
reduce_optimization_subgraph_on_demand (Nenofex * nenofex)
{
  unsigned const int size_limit = nenofex->options.opt_subgraph_limit;
  assert (size_limit > 0);

  unsigned int cur_size = 1;

  Node **ch, *child;
  for (ch = nenofex->changed_subformula.children; (child = *ch); ch++)
    cur_size += child->size_subformula;

  if (cur_size <= size_limit)
    return;

  unsigned int max_size = 0;
  Node *max_size_child = 0;

  for (ch = nenofex->changed_subformula.children; (child = *ch); ch++)
    {
      unsigned int child_size_subformula = child->size_subformula;

      if (child_size_subformula > max_size)
        {
          max_size = child_size_subformula;
          max_size_child = child;
        }
    }                           /* end: for all 'changed'-children */

  assert (max_size_child);

  if (max_size > size_limit)
    {                           /* continue search at max_size_child's children */
      Node *first_child;

    AGAIN:

      first_child = max_size_child->child_list.first;
      max_size_child = 0;
      max_size = 0;

      for (child = first_child; child; child = child->level_link.next)
        {
          unsigned int child_size_subformula = child->size_subformula;

          if (child_size_subformula > max_size)
            {
              max_size = child_size_subformula;
              max_size_child = child;
            }
        }                       /* end: for all max_size_child's children */

      if (max_size > size_limit)
        goto AGAIN;
    }                           /* end: continue search */

  reset_changed_lca_object (nenofex);

  if (!max_size_child
      || (!MAXIMIZE_REDUCED && is_literal_node (max_size_child)))
    return;

  unsigned int reduced_size = max_size_child->size_subformula;

#if MAXIMIZE_REDUCED
  nenofex->changed_subformula.lca = max_size_child->parent;
  reduced_size++;
  add_changed_lca_child (nenofex, max_size_child);

  child = max_size_child->level_link.next;
  if (!child)
    child = max_size_child->parent->child_list.first;

  for (; child != max_size_child;
       child = (child->level_link.next ? child->level_link.next :
                max_size_child->parent->child_list.first))
    {
      if (reduced_size + child->size_subformula <= size_limit)
        {
          reduced_size += child->size_subformula;
          add_changed_lca_child (nenofex, child);
        }
    }                           /* end: for all childlen */

  if (nenofex->changed_subformula.num_children == 1)
    reset_changed_lca_object (nenofex);
#endif

  /* assign new 'changed_subformula ' */

  if (nenofex->changed_subformula.num_children == 0)
    {
      nenofex->changed_subformula.lca = max_size_child;

      for (child = max_size_child->child_list.first;
           child; child = child->level_link.next)
        add_changed_lca_child (nenofex, child);
    }
#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
  assert_pos_in_changed_ch_list (nenofex);
#endif
#endif

  if (nenofex->options.show_progress_specified)
    fprintf (stderr, "Reduced 'Changed-Subgraph' from size %d to size %d\n\n",
             cur_size, reduced_size);
}


static int
simplify_by_global_flow_and_atpg (Nenofex * nenofex)
{
  int graph_modified = 0;

  if (nenofex->changed_subformula.lca)
    {
      if (nenofex->options.opt_subgraph_limit_specified)
        {
          reduce_optimization_subgraph_on_demand (nenofex);

          if (!nenofex->changed_subformula.lca)
            return 0;
        }

      graph_modified = simplify_by_global_flow_and_atpg_main (nenofex);
    }

  return graph_modified;
}


#ifndef NDEBUG
/*
- for assertion checking only
- assert: vars in non-innermost scopes either eliminated (e.g. unates) or costs uninitialized
*/
static void
assert_all_non_innermost_scope_vars_uninitialized (Nenofex * nenofex,
                                                   Scope ** scope)
{
  assert (*scope == find_innermost_non_empty_scope (nenofex));
  scope--;                      /* 'scope' pointed to current scope before */

  void **v_scope, **scope_start;
  scope_start = nenofex->scopes->elems;
  for (v_scope = (void **) scope; v_scope >= scope_start; v_scope--)
    {
      Scope *scope = *v_scope;

      void **v_var;
      for (v_var = scope->vars->top - 1; v_var >= scope->vars->elems; v_var--)
        {
          Var *var = *v_var;
          assert (!variable_has_occs (var) || !var->exp_costs.lca_object.lca
                  || var->scope == *nenofex->next_scope);
        }                       /* end: for all vars in scope */

    }                           /* end: for all scopes */
}
#endif


/* 
- compute graph statistics via full traversal of graph 
- rather expensive
- NOTE: this could also be done incrementally 
*/
static void
compute_graph_statistics (Nenofex * nenofex)
{
/* node count*/
  unsigned int lit_node_cnt = 0;
  unsigned int and_node_cnt = 0;
  unsigned int or_node_cnt = 0;
  unsigned int op_node_cnt = 0;
  unsigned int node_cnt = 0;

/* literal-child count */
  unsigned int max_lit_cnt_per_or_node = 0;
  unsigned int total_lit_cnt_per_or_node = 0;
  float average_lit_cnt_per_or_node = 0;

  unsigned int max_lit_cnt_per_and_node = 0;
  unsigned int total_lit_cnt_per_and_node = 0;
  float average_lit_cnt_per_and_node = 0;

  unsigned int max_lit_cnt_per_op_node = 0;
  unsigned int total_lit_cnt_per_op_node = 0;
  float average_lit_cnt_per_op_node = 0;

/* arity */
  unsigned int max_or_node_arity = 0;
  unsigned int total_or_node_arity = 0;
  float average_arity_per_or_node = 0;

  unsigned int max_and_node_arity = 0;
  unsigned int total_and_node_arity = 0;
  float average_arity_per_and_node = 0;

  unsigned int max_op_node_arity = 0;
  unsigned int total_op_node_arity = 0;
  float average_arity_per_op_node = 0;

/* level */
  unsigned int max_or_node_level = 0;
  unsigned int total_or_node_level = 0;
  float average_level_per_or_node = 0;

  unsigned int max_and_node_level = 0;
  unsigned int total_and_node_level = 0;
  float average_level_per_and_node = 0;

  unsigned int max_lit_node_level = 0;
  unsigned int total_lit_node_level = 0;
  float average_level_per_lit_node = 0;

  unsigned int max_node_level = 0;
  unsigned int total_node_level = 0;
  float average_level_per_node = 0;

  Stack *stack = create_stack (DEFAULT_STACK_SIZE);
  push_stack (stack, nenofex->graph_root);

  Node *cur;
  while ((cur = pop_stack (stack)))
    {
      if (is_literal_node (cur))
        {
          lit_node_cnt++;
          total_lit_node_level += cur->level;

          if (cur->level > max_lit_node_level)
            max_lit_node_level = cur->level;
        }
      else                      /* operator node */
        {
          if (is_or_node (cur))
            {
              or_node_cnt++;
              total_or_node_arity += cur->num_children;

              if (cur->num_children > max_or_node_arity)
                max_or_node_arity = cur->num_children;

              total_or_node_level += cur->level;

              if (cur->level > max_or_node_level)
                max_or_node_level = cur->level;

              Node *ch;
              unsigned int cur_lit_cnt = 0;
              for (ch = cur->child_list.last; ch; ch = ch->level_link.prev)
                {
                  if (is_literal_node (ch))
                    {
                      cur_lit_cnt++;
                      total_lit_cnt_per_or_node++;
                    }
                  push_stack (stack, ch);
                }               /* end: for all children */

              if (cur_lit_cnt > max_lit_cnt_per_or_node)
                max_lit_cnt_per_or_node = cur_lit_cnt;
            }
          else                  /* and */
            {
              and_node_cnt++;
              total_and_node_arity += cur->num_children;

              if (cur->num_children > max_and_node_arity)
                max_and_node_arity = cur->num_children;

              total_and_node_level += cur->level;

              if (cur->level > max_and_node_level)
                max_and_node_level = cur->level;

              Node *ch;
              unsigned int cur_lit_cnt = 0;
              for (ch = cur->child_list.last; ch; ch = ch->level_link.prev)
                {
                  if (is_literal_node (ch))
                    {
                      cur_lit_cnt++;
                      total_lit_cnt_per_and_node++;
                    }
                  push_stack (stack, ch);
                }               /* end: for all children */

              if (cur_lit_cnt > max_lit_cnt_per_and_node)
                max_lit_cnt_per_and_node = cur_lit_cnt;

            }                   /* end: and */
        }                       /* end: op-node */

    }                           /* end: while stack not empty */

  op_node_cnt = or_node_cnt + and_node_cnt;
  node_cnt = op_node_cnt + lit_node_cnt;
  assert (node_cnt == nenofex->graph_root->size_subformula);

  max_lit_cnt_per_op_node =
    (max_lit_cnt_per_or_node >
     max_lit_cnt_per_and_node) ? max_lit_cnt_per_or_node :
    max_lit_cnt_per_and_node;
  total_lit_cnt_per_op_node =
    total_lit_cnt_per_or_node + total_lit_cnt_per_and_node;
  average_lit_cnt_per_op_node =
    (float) total_lit_cnt_per_op_node / op_node_cnt;
  average_lit_cnt_per_or_node =
    (float) total_lit_cnt_per_or_node / or_node_cnt;
  average_lit_cnt_per_and_node =
    (float) total_lit_cnt_per_and_node / and_node_cnt;


  max_op_node_arity = (max_or_node_arity > max_and_node_arity) ?
    max_or_node_arity : max_and_node_arity;
  total_op_node_arity = total_or_node_arity + total_and_node_arity;
  average_arity_per_op_node = (float) total_op_node_arity / op_node_cnt;
  average_arity_per_or_node = (float) total_or_node_arity / or_node_cnt;
  average_arity_per_and_node = (float) total_and_node_arity / and_node_cnt;

  total_node_level =
    total_lit_node_level + total_or_node_level + total_and_node_level;
  max_node_level =
    (max_lit_node_level >
     max_or_node_level) ? max_lit_node_level : max_or_node_level;
  max_node_level =
    (max_node_level >
     max_and_node_level) ? max_node_level : max_and_node_level;

  average_level_per_node = (float) total_node_level / node_cnt;
  average_level_per_lit_node = (float) total_lit_node_level / lit_node_cnt;
  average_level_per_or_node = (float) total_or_node_level / or_node_cnt;
  average_level_per_and_node = (float) total_and_node_level / and_node_cnt;

  FILE *out = stderr;

  fprintf (out, "NODE COUNT:\n");
  fprintf (out, "\ttotal node count: %d\n", node_cnt);
  fprintf (out, "\ttotal op node count: %d\n", op_node_cnt);
  fprintf (out, "\ttotal or node count: %d\n", or_node_cnt);
  fprintf (out, "\ttotal and node count: %d\n", and_node_cnt);
  fprintf (out, "\ttotal lit node count: %d\n", lit_node_cnt);
  fprintf (out, "--\n");

  fprintf (out, "LITERAL-CHILD COUNT:\n");
  fprintf (out, "\tmax lit count per or node: %d\n", max_lit_cnt_per_or_node);
  fprintf (out, "\tmax lit count per and node: %d\n",
           max_lit_cnt_per_and_node);
  fprintf (out, "\tmax lit count per op node: %d\n", max_lit_cnt_per_op_node);
  fprintf (out, "\taverage lit count per or node: %f\n",
           average_lit_cnt_per_or_node);
  fprintf (out, "\taverage lit count per and node: %f\n",
           average_lit_cnt_per_and_node);
  fprintf (out, "\taverage lit count per op node: %f\n",
           average_lit_cnt_per_op_node);
  fprintf (out, "--\n");

  fprintf (out, "ARITY:\n");
  fprintf (out, "\tmax or node arity: %d\n", max_or_node_arity);
  fprintf (out, "\tmax and node arity : %d\n", max_and_node_arity);
  fprintf (out, "\tmax op node arity: %d\n", max_op_node_arity);
  fprintf (out, "\taverage arity per or node: %f\n",
           average_arity_per_or_node);
  fprintf (out, "\taverage arity per and node: %f\n",
           average_arity_per_and_node);
  fprintf (out, "\taverage arity per op node: %f\n",
           average_arity_per_op_node);
  fprintf (out, "--\n");

  fprintf (out, "LEVEL:\n");
  fprintf (out, "\tmax or node level: %d\n", max_or_node_level);
  fprintf (out, "\tmax and node level: %d\n", max_and_node_level);
  fprintf (out, "\tmax lit node level: %d\n", max_lit_node_level);
  fprintf (out, "\tmax node level: %d\n", max_node_level);
  fprintf (out, "\taverage level per or node: %f\n",
           average_level_per_or_node);
  fprintf (out, "\taverage level per and node: %f\n",
           average_level_per_and_node);
  fprintf (out, "\taverage level per lit node: %f\n",
           average_level_per_lit_node);
  fprintf (out, "\taverage level per node: %f\n", average_level_per_node);
  fprintf (out, "--\n");

  delete_stack (stack);
}


#if APPROXIMATE_UNIV_SCORES
/*
- initialize all variables in a scope from scratch
- priority order has to be established manually afetrwards by calling 'init_order'
*/
static void
approximate_univ_scope_variable_scores (Nenofex * nenofex, Scope * scope)
{
  void **v_var, **end;
  end = scope->vars->top;

  unsigned int lits = 0, min_lits = UINT_MAX;
  Var *min_lit_var = 0;

  for (v_var = scope->vars->elems; v_var < end; v_var++)
    {
      Var *var = *v_var;

      if ((lits = (var->lits[0].occ_cnt + var->lits[1].occ_cnt)) > 0
          && lits < min_lits)
        {
          min_lits = lits;
          min_lit_var = var;
        }

      lca_update_unmark (var);
      inc_score_update_unmark (var);
      dec_score_update_unmark (var);
    }                           /* end: for all vars in scope */

  if (!min_lit_var)             /* empty scope */
    return;

  lca_update_mark (min_lit_var);
  inc_score_update_mark (min_lit_var);
  dec_score_update_mark (min_lit_var);

  init_variable_scores (nenofex, min_lit_var, 0);

  lca_update_unmark (min_lit_var);
  inc_score_update_unmark (min_lit_var);
  dec_score_update_unmark (min_lit_var);

  for (v_var = scope->vars->elems; v_var < end; v_var++)
    {
      Var *var = *v_var;
      if (var != min_lit_var)
        {
          var->exp_costs.score = min_lit_var->exp_costs.score + 1;
          var->exp_costs.inc_score = var->exp_costs.score;
          var->exp_costs.dec_score = 0;
        }
    }
}
#endif

/*
- initialize all variables in a scope from scratch
- priority order has to be established manually afetrwards by calling 'init_order'
*/
static void
init_all_scope_variable_scores (Nenofex * nenofex, Scope * scope)
{
#if APPROXIMATE_UNIV_SCORES
  if (nenofex->consider_univ_exp && nenofex->cur_scope
      && is_existential_scope (*nenofex->cur_scope) && nenofex->next_scope
      && is_universal_scope (*nenofex->next_scope))
    {
      approximate_univ_scope_variable_scores (nenofex, scope);
      return;
    }
#endif

  void **v_var, **end;
  end = scope->vars->top;

  for (v_var = scope->vars->elems; v_var < end; v_var++)
    {
      Var *var = *v_var;

      lca_update_mark (var);
      inc_score_update_mark (var);
      dec_score_update_mark (var);

      init_variable_scores (nenofex, var, 0);

      lca_update_unmark (var);
      inc_score_update_unmark (var);
      dec_score_update_unmark (var);
    }                           /* end: for all vars in scope */
}


/*
- find cheapest univrsal variable from non-innermost universal scope
- actually called only after prior call of 'peek_min_cost_universal_var_in_next_scope'
*/
static Var *
find_min_cost_universal_var_in_next_scope (Nenofex * nenofex,
                                           Scope * universal_scope,
                                           const int init_scope_vars)
{
  assert (is_universal_scope (universal_scope));
  assert (universal_scope == *nenofex->next_scope);
  assert (is_existential_scope (*nenofex->cur_scope));

  if (init_scope_vars)
    {                           /* NOTE: rather expensive */
      if (nenofex->options.show_progress_specified)
        fprintf (stderr, "Initializing non-innermost universal scope...\n");
      init_all_scope_variable_scores (nenofex, universal_scope);
      init_order_priority_queue (universal_scope->priority_heap);
    }

  if (nenofex->options.show_progress_specified)
    fprintf (stderr, "\thas total %d variables, remaining %d\n",
             count_stack (universal_scope->vars),
             count_stack (universal_scope->priority_heap));

  Var *min_cost_var = 0;
  Stack *scope_priority_heap = universal_scope->priority_heap;

  while ((min_cost_var = remove_min (scope_priority_heap)))
    {
      if (variable_has_occs (min_cost_var))
        break;
      else
        {
          assert (0);
        }
    }

  nenofex->num_cur_remaining_scope_vars = count_stack (scope_priority_heap);

  if (min_cost_var && nenofex->options.show_progress_specified)
    fprintf (stderr, "\tfound non-innermost univ. var %d with cost %d\n",
             min_cost_var->id, min_cost_var->exp_costs.score);

  return min_cost_var;
}


/*
- like 'find_min_cost_universal_var_in_next_scope' but does not remove var from p.queue
*/
static Var *
peek_min_cost_universal_var_in_next_scope (Nenofex * nenofex,
                                           Scope * universal_scope,
                                           const int init_scope_vars)
{
  assert (is_universal_scope (universal_scope));
  assert (universal_scope == (*nenofex->next_scope));
  assert (is_existential_scope (*nenofex->cur_scope));

  if (init_scope_vars)
    {                           /* NOTE: rather expensive */
      if (nenofex->options.show_progress_specified)
        fprintf (stderr, "Initializing non-innermost universal scope...\n");
      init_all_scope_variable_scores (nenofex, universal_scope);
      init_order_priority_queue (universal_scope->priority_heap);
    }

  Var *min_cost_var = 0;
  Stack *scope_priority_heap = universal_scope->priority_heap;

  while ((min_cost_var = access_min (scope_priority_heap)))
    {
      if (variable_has_occs (min_cost_var))
        break;
      else
        {
          assert (0);
        }
    }

  return min_cost_var;
}


static void
copy_and_add_depending_variable (Nenofex * nenofex, Var * depending_var)
{
  Var *var_copy = (Var *) mem_malloc (sizeof (Var));
  assert (var_copy);
  memset (var_copy, 0, sizeof (Var));

  var_copy->pos_in_lca_child_list_occs = create_stack (DEFAULT_STACK_SIZE);

  var_copy->exp_costs.score = INT_MIN;

  init_lca_object (&(var_copy->exp_costs.lca_object));

  var_copy->id = depending_var->id;     /* var-IDs do not matter at all */
  var_copy->priority_pos = -1;
  var_copy->lits[0].var = var_copy;
  var_copy->lits[0].negated = 1;
  var_copy->lits[1].var = var_copy;
  var_copy->lits[1].negated = 0;
  var_copy->lits[0].occ_list.first = (Node *) 0;
  var_copy->lits[0].occ_list.last = (Node *) 0;
  var_copy->lits[1].occ_list.first = (Node *) 0;
  var_copy->lits[1].occ_list.last = (Node *) 0;

  Scope *scope = depending_var->scope;
  push_stack (scope->vars, var_copy);
  scope->remaining_var_cnt++;

  add_fast_priority_queue (scope->priority_heap, var_copy);

  var_copy->scope = scope;

  depending_var->copied = var_copy;

  collect_variable_as_unate (nenofex, var_copy);

  /* copied variables' scores will have to be updated before next expansion */
  lca_update_mark (var_copy);
  inc_score_update_mark (var_copy);
  dec_score_update_mark (var_copy);
  collect_variable_for_update (nenofex, var_copy);
}


static void
copy_innermost_depending_existential_variables (Nenofex * nenofex)
{
  void **v_var, **end;
  end = nenofex->depending_vars->top;

  for (v_var = nenofex->depending_vars->elems; v_var < end; v_var++)
    {
      Var *var = *v_var;
      assert (var->collected_as_depending);
      assert (is_existential_scope (var->scope));
      assert (var->scope == (*nenofex->cur_scope));
      assert (!var->copied);

      copy_and_add_depending_variable (nenofex, var);

      /* depending variables' scores will be updated before next expansion */
      lca_update_mark (var);
      inc_score_update_mark (var);
      dec_score_update_mark (var);
      collect_variable_for_update (nenofex, var);
    }                           /* end: for all depending variables */

  /* TODO: avoid complete recalculation of order */
  init_order_priority_queue ((*nenofex->cur_scope)->priority_heap);
}


/*
- must be called before a universal variable from non-innermost scope is expanded
- collect and copy depending existential variables
- NOTE: set of depending vars is known after LCA-computation -> need not recompute 
*/
static void
prepare_non_innermost_universal_expansion (Nenofex * nenofex,
                                           Var * universal_var)
{
  assert (is_universal_scope (universal_var->scope));
  assert (universal_var->scope == *nenofex->next_scope);
  assert (!count_stack (nenofex->depending_vars));
#ifndef NDEBUG
#if ASSERT_PREPARE_NON_INNERMOST_UNIVERSAL_EXPANSION
  assert_no_var_collected_as_depending (nenofex);
#endif
#endif

  /* TODO: not necessary if depending vars are stored after LCA computation */
  collect_innermost_depending_existential_variables (universal_var,
                                                     nenofex->depending_vars,
                                                     0);

  if (nenofex->options.show_progress_specified)
    fprintf (stderr, "\tvar. has %d depending existential variables\n",
             count_stack (nenofex->depending_vars));

  copy_innermost_depending_existential_variables (nenofex);
}


/*
- compute scores of variables which have been collected for update
*/
static void
collected_variables_update_scores (Nenofex * nenofex)
{
#if (PRINT_INCREMENTAL_COST_UPDATE || COMPUTE_AVERAGE_UPDATE_MARKED_RATIOS)
  unsigned int cnt_update_lca = 0;
  unsigned int cnt_update_inc_score = 0;
  unsigned int cnt_update_dec_score = 0;
#endif

  Stack *collected_variables = nenofex->vars_marked_for_update;

  Var *update_var;
  while ((update_var = pop_stack (collected_variables)))
    {
      assert (update_var->collected_for_update);
      assert (update_var->scope == *nenofex->cur_scope);

      update_var->collected_for_update = 0;

      if (!cost_update_marked (update_var))
        continue;

      if (variable_has_occs (update_var))
        {                       /* update */
          init_variable_scores (nenofex, update_var, 1);

#if (PRINT_INCREMENTAL_COST_UPDATE || COMPUTE_AVERAGE_UPDATE_MARKED_RATIOS)
          if (lca_update_marked (update_var))
            cnt_update_lca++;
          if (inc_score_update_marked (update_var))
            cnt_update_inc_score++;
          if (dec_score_update_marked (update_var))
            cnt_update_dec_score++;
#endif
        }

      lca_update_unmark (update_var);
      inc_score_update_unmark (update_var);
      dec_score_update_unmark (update_var);
    }                           /* end: for all marked variables */

#if PRINT_INCREMENTAL_COST_UPDATE
  fprintf (stderr,
           "Found %d vars lca-marked from %d vars remaining in scope\n",
           cnt_update_lca,
           count_stack ((*nenofex->cur_scope)->priority_heap));
  fprintf (stderr,
           "Found %d vars inc_score-marked from %d vars remaining in scope\n",
           cnt_update_inc_score,
           count_stack ((*nenofex->cur_scope)->priority_heap));
  fprintf (stderr,
           "Found %d vars dec_score-marked from %d vars remaining in scope\n",
           cnt_update_dec_score,
           count_stack ((*nenofex->cur_scope)->priority_heap));
#endif

#if COMPUTE_AVERAGE_UPDATE_MARKED_RATIOS
  unsigned int remaining = count_stack ((*nenofex->cur_scope)->priority_heap);
  nenofex->stats.sum_remaining += remaining;
  if (remaining == 0)
    remaining = 1;
  nenofex->stats.sum_lca_marked += cnt_update_lca;
  nenofex->stats.sum_inc_marked += cnt_update_inc_score;
  nenofex->stats.sum_dec_marked += cnt_update_dec_score;
  nenofex->stats.sum_ratio_lca_marked_in_scope_vars +=
    ((double) cnt_update_lca / remaining);
  nenofex->stats.sum_ratio_inc_marked_in_scope_vars +=
    ((double) cnt_update_inc_score / remaining);
  nenofex->stats.sum_ratio_dec_marked_in_scope_vars +=
    ((double) cnt_update_dec_score / remaining);
#endif
}


#define move_scope(scope, scopes_start) \
if (scope) \
{ \
  assert(scope >= (Scope **) scopes_start); \
  while(scope > (Scope **) scopes_start) \
  { \
    scope--; \
    if (!is_empty_scope(nenofex, *scope)) \
      break; \
    else \
    (*scope)->is_empty = 1; \
  } \
  if (scope == (Scope **) scopes_start) \
    scope = 0; \
}


/*
- possible situation: 'next_scope' and 'cur_scope' are both of the same kind
- may happen if the scope between has become empty
- merge 'next_scope' and 'cur_scope' scope into one and find new 'next_scope'
*/
static void
merge_cur_and_next_scope (Nenofex * nenofex)
{
AGAIN:

  assert (nenofex->cur_scope);
  assert (nenofex->next_scope);

  Scope *cur_scope = *nenofex->cur_scope;
  Scope *next_scope = *nenofex->next_scope;
  assert (cur_scope->type == next_scope->type);

  if (nenofex->options.show_progress_specified)
    fprintf (stderr, "Merging scopes %d and %d...\n",
             next_scope->nesting, cur_scope->nesting);

  Stack *cur_scope_vars = cur_scope->vars;
  Stack *next_scope_vars = next_scope->vars;

  next_scope->remaining_var_cnt += cur_scope->remaining_var_cnt;
  cur_scope->remaining_var_cnt = 0;

  Var *var;
  while ((var = pop_stack (cur_scope_vars)))
    {                           /* move vars from 'cur_scope' to 'next_scope' scope */
      var->exp_costs.score = INT_MIN;

      if (var->exp_costs.lca_object.lca)
        {
          unlink_variable_from_lca_list (var);
          reset_lca_object (nenofex, var, &(var)->exp_costs.lca_object, 1);
        }

      var->priority_pos = -1;
      push_stack (next_scope_vars, var);
      var->scope = next_scope;

      if (variable_has_occs (var))
        add_fast_priority_queue (next_scope->priority_heap, var);
    }                           /* end: for all variables in 'cur_scope' */

  reset_stack (cur_scope->priority_heap);
  cur_scope->is_empty = 1;

  /* initialize 'next_scope' from scratch */
  init_all_scope_variable_scores (nenofex, next_scope);
  init_order_priority_queue (next_scope->priority_heap);

  /* find new 'next_scope' */
  nenofex->cur_scope = nenofex->next_scope;
  move_scope (nenofex->next_scope, nenofex->scopes->elems);

  if (nenofex->next_scope &&
      (*nenofex->next_scope)->type == (*nenofex->cur_scope)->type)
    goto AGAIN;
}


/*
- expand either the cheapest exist. var from innermost scope or a univ. var from next scope
- pointers to 'next_scope' and 'cur_scope' are maintained
- a scope is moved to the next outer scope if it becomes empty
- if 'next_scope' and 'cur_scope' are both of the same 
   type they will be merged into one scope
*/
static Var *
pick_variable_for_expansion (Nenofex * nenofex)
{
  assert (nenofex->cur_scope);
  assert (!nenofex->next_scope ||
          (*nenofex->next_scope)->type != (*nenofex->cur_scope)->type);

  collected_variables_update_scores (nenofex);

  assert (!nenofex->consider_univ_exp || nenofex->next_scope);
  assert (!nenofex->consider_univ_exp || !nenofex->next_scope ||
          (is_existential_scope (*nenofex->cur_scope) &&
           is_universal_scope (*nenofex->next_scope)));

  Var *min_var = 0, *min_cur_scope_var = 0, *min_next_scope_var = 0;

  min_cur_scope_var =
    peek_min_cost_var_in_scope (nenofex, *nenofex->cur_scope);

  if (!min_cur_scope_var)
    {                           /* cur. scope is empty -> move both next and cur. scope */
      (*nenofex->cur_scope)->is_empty = 1;
      assert (is_empty_scope (nenofex, *nenofex->cur_scope));

      move_scope (nenofex->next_scope, nenofex->scopes->elems);
      move_scope (nenofex->cur_scope, nenofex->scopes->elems);

      assert (nenofex->cur_scope
              && nenofex->cur_scope > (Scope **) nenofex->scopes->elems);

      /* both scopes were empty */
      if (nenofex->cur_scope == nenofex->next_scope)
        move_scope (nenofex->next_scope, nenofex->scopes->elems);

      if (nenofex->next_scope &&
          (*nenofex->next_scope)->type == (*nenofex->cur_scope)->type)
        {                       /* merge scopes */
          merge_cur_and_next_scope (nenofex);
        }

      assert (!nenofex->next_scope ||
              nenofex->next_scope > (Scope **) nenofex->scopes->elems);

      /* initialize cur. scope */
      init_all_scope_variable_scores (nenofex, *nenofex->cur_scope);
      init_order_priority_queue ((*nenofex->cur_scope)->priority_heap);

      if (!nenofex->next_scope
          || (is_existential_scope (*nenofex->next_scope)
              && is_universal_scope (*nenofex->cur_scope)))
        nenofex->consider_univ_exp = 0;

      min_cur_scope_var =
        peek_min_cost_var_in_scope (nenofex, *nenofex->cur_scope);
    }                           /* end: cur. scope is empty */
  assert (min_cur_scope_var);

  if (nenofex->next_scope)
    {

      if (nenofex->consider_univ_exp
          && is_universal_scope (*nenofex->next_scope)
          && is_existential_scope (*nenofex->cur_scope))
        {                       /* next scope is univ., cur is exist. -> pick variable on demand only (expensive score comp.) */
          min_next_scope_var =
            peek_min_cost_universal_var_in_next_scope (nenofex,
                                                       *nenofex->next_scope,
                                                       1);

          if (!min_next_scope_var)
            {                   /* next scope is empty */
              (*nenofex->next_scope)->is_empty = 1;
              assert (is_empty_scope (nenofex, *nenofex->next_scope));

              move_scope (nenofex->next_scope, nenofex->scopes->elems);

              if (nenofex->next_scope &&
                  (*nenofex->next_scope)->type == (*nenofex->cur_scope)->type)
                {               /* merge scopes */
                  merge_cur_and_next_scope (nenofex);
                }

              assert (!nenofex->next_scope ||
                      nenofex->next_scope >
                      (Scope **) nenofex->scopes->elems);
              assert (!nenofex->next_scope
                      || (is_universal_scope (*nenofex->next_scope)
                          && is_existential_scope (*nenofex->cur_scope)));

              if (nenofex->next_scope)
                min_next_scope_var =
                  peek_min_cost_universal_var_in_next_scope (nenofex,
                                                             *nenofex->
                                                             next_scope, 1);
              else
                nenofex->consider_univ_exp = 0;
            }
        }                       /* end: possible non-innermost universal expansion */
    }                           /* end: next scope exists */

  if (nenofex->consider_univ_exp && min_next_scope_var)
    {                           /* if univ. expansion enabled, then expand regardless of cheapest exist. var. */
      assert (is_universal_scope (*nenofex->next_scope) &&
              is_existential_scope (*nenofex->cur_scope));
      min_var =
        find_min_cost_universal_var_in_next_scope (nenofex,
                                                   *nenofex->next_scope, 0);
      prepare_non_innermost_universal_expansion (nenofex, min_var);
      nenofex->consider_univ_exp = 0;
    }
  else
    {
      min_var = find_min_cost_var_in_scope (nenofex, *nenofex->cur_scope);
    }

  assert (min_var);
  assert (!nenofex->consider_univ_exp);
  assert (!nenofex->next_scope ||
          (*nenofex->next_scope)->type != (*nenofex->cur_scope)->type);

  return min_var;
}


#ifndef NDEBUG
static void
assert_all_scope_variable_counts (Nenofex * nenofex)
{
  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;
      int remaining = 0;
      void **v_var, **var_end;
      var_end = scope->vars->top;
      for (v_var = scope->vars->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;
          if (variable_has_occs (var))
            remaining++;
        }                       /* end: for all vars in scope */

      assert (remaining == scope->remaining_var_cnt);
    }                           /* end: for all scopes */
}
#endif


#ifndef NDEBUG
static void
assert_all_scopes_free_of_no_occ_vars (Nenofex * nenofex)
{
  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;
      void **v_var, **var_end;
      var_end = scope->priority_heap->top;
      for (v_var = scope->priority_heap->elems; v_var < var_end; v_var++)
        {
          Var *var = *v_var;
          assert (variable_has_occs (var));
        }                       /* end: for all vars in scope */
    }                           /* end: for all scopes */
}
#endif


#ifndef NDEBUG
static void
assert_all_scopes_priority_queue_heap_condition (Nenofex * nenofex)
{
  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;
      assert_priority_queue_heap_condition (scope->priority_heap);
    }                           /* end: for all scopes */
}
#endif


static void
print_lit_stats_before_exp (Nenofex * nenofex, Var * var)
{
#if 1
  if (var->exp_costs.score <= 50)
    return;
#endif

  int neg_lit_count = 0;
  int pos_lit_count = 0;

  Lit *lit = var->lits;
  Node *lit_node;
  for (lit_node = lit->occ_list.first; lit_node;
       lit_node = lit_node->occ_link.next)
    neg_lit_count++;

  lit++;
  for (lit_node = lit->occ_list.first; lit_node;
       lit_node = lit_node->occ_link.next)
    pos_lit_count++;

  /* Estimate how many nodes could be shared. This is done by detecting 
     which subtrees' functions are not affected by expansion. */
  LCAObject *lcaobj = &var->exp_costs.lca_object;
  Stack *stack = create_stack (1);
  Node **childpp, *childp, *tmp;

#ifndef NDEBUG
#if ASSERT_PRINT_LIT_STATS_BEFORE_EXP
  /* Make sure that no node is marked. */
  assert (count_stack (stack) == 0);
  do
    {
      for (childpp = lcaobj->children; (childp = *childpp); childpp++)
        push_stack (stack, childp);

      while ((childp = pop_stack (stack)))
        {
          assert (!childp->mark1);
          for (childp = childp->child_list.first; childp;
               childp = childp->level_link.next)
            push_stack (stack, childp);
        }
    }
  while (0);
#endif
#endif


  /* Mark ancestors of literals up to LCA. */
  lit = var->lits;
  for (lit_node = lit->occ_list.first; lit_node;
       lit_node = lit_node->occ_link.next)
    {
      tmp = lit_node;
      do
        {
          tmp->mark1 = 1;
          tmp = tmp->parent;
        }
      while (tmp && tmp->level >= lcaobj->lca->level);
    }

  lit++;
  for (lit_node = lit->occ_list.first; lit_node;
       lit_node = lit_node->occ_link.next)
    {
      tmp = lit_node;
      do
        {
          tmp->mark1 = 1;
          tmp = tmp->parent;
        }
      while (tmp && tmp->level >= lcaobj->lca->level);
    }

  unsigned int total_size = 0;
  assert (lcaobj->lca->mark1);

  /* Traverse LCA-subtree and count subtree sizes. */
  assert (count_stack (stack) == 0);
  for (childpp = lcaobj->children; (childp = *childpp); childpp++)
    push_stack (stack, childp);

  while ((childp = pop_stack (stack)))
    {

      if (!childp->mark1)
        {
          total_size = childp->size_subformula;
        }
      else
        {
          childp->mark1 = 0;
          for (childp = childp->child_list.first; childp;
               childp = childp->level_link.next)
            push_stack (stack, childp);
        }
    }

#ifndef NDEBUG
#if ASSERT_PRINT_LIT_STATS_BEFORE_EXP
  /* Make sure that no node is marked. */
  assert (count_stack (stack) == 0);
  do
    {
      for (childpp = lcaobj->children; (childp = *childpp); childpp++)
        push_stack (stack, childp);

      while ((childp = pop_stack (stack)))
        {
          assert (!childp->mark1);
          for (childp = childp->child_list.first; childp;
               childp = childp->level_link.next)
            push_stack (stack, childp);
        }
    }
  while (0);
#endif
#endif

  delete_stack (stack);

  fprintf (stderr, "\nBEFORE INNERMOST EXP: LIT STATS\n");

  fprintf (stderr,
           "Var %d has %d neg. lits, %d pos. lits and total %d lits\n",
           var->id, neg_lit_count, pos_lit_count,
           neg_lit_count + pos_lit_count);

  fprintf (stderr, "Var %d has %d inc-score, %d dec-score, %d total-score\n",
           var->id, var->exp_costs.inc_score, var->exp_costs.dec_score,
           var->exp_costs.inc_score - var->exp_costs.dec_score);

  fprintf (stderr,
           "Var %d summary: %d total-lits, %d total-cost, %d sharing-potential\n\n",
           var->id, neg_lit_count + pos_lit_count,
           var->exp_costs.inc_score - var->exp_costs.dec_score, total_size);

}


/*
- core function: eliminate vars from innermost scope based on expansion costs
- abort according to specified options or if result is known
- NEW: expansion of univ. var. from non-innermost scope (must be enabled first)
*/
static void
expansion_phase (Nenofex * nenofex)
{
  assert (!nenofex->is_existential);
  assert (!nenofex->is_universal);
  assert (!nenofex->consider_univ_exp);
  assert (((Scope *) * nenofex->scopes->elems)->nesting ==
          DEFAULT_SCOPE_NESTING);

#if 1                           /* WORKAROUND: remove vars without any occs after parsing 
                                   (violation of QDIMACS standard) */
  simplify_eliminate_unates (nenofex);
#endif

  Var *var = 0;
  nenofex->cur_scope = (Scope **) nenofex->scopes->top - 1;
  assert (nenofex->cur_scope >= (Scope **) nenofex->scopes->elems);

  assert (!nenofex->next_scope);
  if (nenofex->cur_scope - 1 > (Scope **) nenofex->scopes->elems)
    {                           /* typically QBF */
      nenofex->next_scope = nenofex->cur_scope - 1;
      assert (nenofex->cur_scope > (Scope **) nenofex->scopes->elems);
      /* default scope unused */
      (*((Scope **) nenofex->scopes->elems))->is_empty = 1;
      assert (is_empty_scope
              (nenofex, (*((Scope **) nenofex->scopes->elems))));
    }
  else if (nenofex->cur_scope - 1 == (Scope **) nenofex->scopes->elems)
    {                           /* default scope unused */
      (*((Scope **) nenofex->scopes->elems))->is_empty = 1;
      assert (is_empty_scope
              (nenofex, (*((Scope **) nenofex->scopes->elems))));
    }

  /* initialize cur. scope from scratch once at startup */
  init_all_scope_variable_scores (nenofex, *nenofex->cur_scope);
  init_order_priority_queue ((*nenofex->cur_scope)->priority_heap);

  /* NOTE: calling 'is_existential/universal' could be postponed; 
     should clean up vars without occurrences first */
  nenofex->is_existential = is_formula_existential (nenofex);
  nenofex->is_universal = is_formula_universal (nenofex);

  int full = nenofex->options.full_expansion_specified;
  int limit = nenofex->options.num_expansions_specified;

  int size_cutoff_relative_specified =
    nenofex->options.size_cutoff_relative_specified;
  int size_cutoff_absolute_specified =
    nenofex->options.size_cutoff_absolute_specified;
  assert (!size_cutoff_relative_specified || !size_cutoff_absolute_specified);

  int size_cutoff_specified =
    size_cutoff_relative_specified || size_cutoff_absolute_specified;
  unsigned int size_before_expansion, size_after_expansion;
  size_before_expansion = size_after_expansion = 0;
  int size_increase = 0;

  int cost_cutoff_specified = nenofex->options.cost_cutoff_specified;
  int cost_cutoff = nenofex->options.cost_cutoff;

  int optimize = !nenofex->options.no_optimizations_specified;
  optimize = optimize && (!nenofex->options.no_atpg_specified ||
                          !nenofex->options.no_global_flow_specified);

  /* Use either current size increase as universal trigger or measure
     absolute value of nodes in formula. */
  int universal_trigger;
  if (!nenofex->options.univ_trigger_abs)
    universal_trigger = nenofex->options.univ_trigger;
  else
    universal_trigger = nenofex->graph_root->size_subformula +
      (nenofex->graph_root->size_subformula *
       nenofex->options.univ_trigger_delta / 100);

  nenofex->cur_expansions = 0;
  while (nenofex->result == NENOFEX_RESULT_UNKNOWN && ((full && !limit) || (full && limit && nenofex->cur_expansions < nenofex->options.num_expansions) || (!full && !limit && !nenofex->is_existential && !nenofex->is_universal) ||   /* std. case */
                                                       (!full && limit
                                                        && nenofex->
                                                        cur_expansions <
                                                        nenofex->options.
                                                        num_expansions
                                                        && !nenofex->
                                                        is_existential
                                                        && !nenofex->
                                                        is_universal)))
    {

      if (nenofex->options.show_graph_size_specified)
        {
          fprintf (stderr, "Graph size: %d\n\n",
                   nenofex->graph_root->size_subformula);
        }

#if COMPUTE_MAX_TREE_SIZE
      if (nenofex->graph_root)
        {
          unsigned int cur_size = nenofex->graph_root->size_subformula;
          if (cur_size > nenofex->stats.max_tree_size)
            nenofex->stats.max_tree_size = cur_size;
        }
#endif

#if PRINT_CLAUSE_COUNT
      int clause_count = nnf_to_cnf_tseitin_revised_count_clauses (nenofex);
      fprintf (stderr, "Clause Count = %d\n\n", clause_count);
#endif

#if COMPUTE_GRAPH_STATISTICS
      compute_graph_statistics (nenofex);
#endif

      assert (!nenofex->changed_subformula.lca ||
              !is_literal_node (nenofex->changed_subformula.lca));
      assert (!nenofex->changed_subformula.lca ||
              nenofex->changed_subformula.num_children >= 2);

#ifndef NDEBUG
#if ASSERT_POS_IN_CHANGED_CH_LIST
      if (nenofex->changed_subformula.lca)
        assert_pos_in_changed_ch_list (nenofex);
#endif
#endif

      assert (nenofex->cur_scope);
      assert (nenofex->cur_scope >= (Scope **) nenofex->scopes->elems);
      assert (!nenofex->next_scope ||
              nenofex->next_scope >= (Scope **) nenofex->scopes->elems);
      assert (nenofex->next_scope < (Scope **) nenofex->scopes->elems ||
              nenofex->next_scope);

      assert (!nenofex->atpg_rr_called);
      assert (!nenofex->atpg_rr_reset_changed_subformula);

      if (simplify_eliminate_units (nenofex))
        continue;

      int unates;
      if ((unates = simplify_eliminate_unates (nenofex)))
        {
          if (nenofex->options.show_progress_specified)
            fprintf (stderr, "Found %d unates\n", unates);
          continue;
        }

#ifndef NDEBUG
#if ASSERT_SCOPE_VAR_CNT
      assert_all_scope_variable_counts (nenofex);
#endif
#endif


/*
- NOTE: on CNFs, ATPG/GlobalFlow do rather not succeed after exp. of negative-score-vars?
*/

      if (optimize)
        {
          nenofex->performed_optimizations++;

          if (simplify_by_global_flow_and_atpg (nenofex))
            {
              nenofex->successful_optimizations++;

              if (nenofex->cur_expansions > 0
                  && !nenofex->first_successful_opt)
                nenofex->first_successful_opt = nenofex->cur_expansions;

#ifndef NDEBUG
#if ASSERT_GRAPH_AFTER_ATPG_GLOBAL_FLOW
              assert (!nenofex->cur_expanded_var);
              assert_all_child_occ_lists_integrity (nenofex);
              assert_all_occ_lists_integrity (nenofex);
              assert_all_subformula_sizes (nenofex);
#endif
#endif
              continue;
            }
        }                       /* end: if optimizations enabled */
      else if (1)               /* NOTE: reset 'changed-subgraph' if no opt. performed (?) */
        reset_changed_lca_object (nenofex);

#ifndef NDEBUG
#if ASSERT_ALL_SCOPES_FREE_OF_NO_OCC_VARS
      assert_all_scopes_free_of_no_occ_vars (nenofex);
#endif
#endif

      /* check if formula is exist. / univ. - NOTE: this could be done incrementally */
      if (!full && nenofex->result == NENOFEX_RESULT_UNKNOWN)
        {
          nenofex->is_existential = is_formula_existential (nenofex);
          if (!nenofex->is_existential)
            nenofex->is_universal = is_formula_universal (nenofex);
          else
            nenofex->is_universal = 0;
        }
      else if (!full)
        {
          assert (!nenofex->is_existential);
          assert (!nenofex->is_universal);
        }

      if (!full && (nenofex->is_existential || nenofex->is_universal))
        {                       /* will exit loop immediately */
          nenofex->sat_solver_tautology_mode = nenofex->is_universal;
          continue;
        }
      /* end: check if formula is existential */

      if (nenofex->graph_root)
        size_before_expansion = nenofex->graph_root->size_subformula;
      else
        size_before_expansion = 0;

      int non_inner_univ_exp = nenofex->consider_univ_exp;
      var = pick_variable_for_expansion (nenofex);

#ifndef NDEBUG
#if  ASSERT_ALL_SCOPES_PRIORITY_QUEUE_HEAP_CONDITION
      assert_all_scopes_priority_queue_heap_condition (nenofex);
#endif
#endif

      assert (!nenofex->cur_scope ||
              *nenofex->cur_scope ==
              find_innermost_non_empty_scope (nenofex));
      assert (var);

      /* check if cost-cutoff specified */
      if (cost_cutoff_specified)
        {
          if (var->exp_costs.score > cost_cutoff)
            {
              fprintf (stderr,
                       "\n\tCOST CUTOFF: cutoff = %d, current cost = %d\n\n",
                       cost_cutoff, var->exp_costs.score);
              break;
            }
        }                       /* end: cost_cutoff specified */

      if (nenofex->options.show_progress_specified)
        {
          if ((*nenofex->cur_scope)->nesting != DEFAULT_SCOPE_NESTING)
            {
              fprintf (stderr, "Expansion (%c): var %d, cost %d from "
                       "scope %d (%d remaining, %d in next scope)\n\n",
                       is_existential_scope (var->scope) ? 'E' : 'A',
                       var->id, var->exp_costs.score, var->scope->nesting,
                       nenofex->num_cur_remaining_scope_vars,
                       nenofex->next_scope ? (*nenofex->next_scope)->
                       remaining_var_cnt : 0);
            }
          else
            {
              fprintf (stderr,
                       "Expanding min. cost var (%c): %d with cost %d "
                       "(with %d variables remaining)\n\n",
                       is_existential_scope (var->scope) ? 'E' : 'A', var->id,
                       var->exp_costs.score,
                       nenofex->num_cur_remaining_scope_vars);
            }
        }

      mark_affected_scope_variables_for_cost_update (nenofex,
                                                     var->exp_costs.
                                                     lca_object.lca);

#if PRINT_LIT_STATS_BEFORE_EXP
      if (var->scope->nesting == (*nenofex->cur_scope)->nesting)
        print_lit_stats_before_exp (nenofex, var);
#endif

      if (is_existential_scope (var->scope))
        {
          assert (!non_inner_univ_exp);
          expand_existential_variable (nenofex, var);
        }
      else
        {
          expand_universal_variable (nenofex, var);
          if (non_inner_univ_exp && nenofex->options.univ_trigger_abs)
            {
              int new_trigger = nenofex->graph_root->size_subformula +
                (nenofex->graph_root->size_subformula *
                 nenofex->options.univ_trigger_delta / 100);
              if (new_trigger > universal_trigger)
                {
                  universal_trigger = new_trigger;
                  if (nenofex->options.show_progress_specified)
                    fprintf (stderr, "New univ-trigger-abs: %d\n",
                             universal_trigger);
                }
            }
        }

      nenofex->cur_expansions++;
      if (nenofex->graph_root)
        size_after_expansion = nenofex->graph_root->size_subformula;
      else
        size_after_expansion = 0;
      size_increase = size_after_expansion - size_before_expansion;

      if (nenofex->next_scope &&
          is_existential_scope (var->scope) &&
          ((!nenofex->options.univ_trigger_abs
            && (size_increase > universal_trigger))
           ||
           ((nenofex->options.univ_trigger_abs
             && (nenofex->graph_root
                 && nenofex->graph_root->size_subformula >
                 universal_trigger))))
          && is_universal_scope (*nenofex->next_scope)
          && is_existential_scope (*nenofex->cur_scope))
        {
          nenofex->consider_univ_exp = 1;

          if (!nenofex->options.univ_trigger_abs)
            universal_trigger += nenofex->options.univ_trigger_delta;
          else
            {
#if 0
              universal_trigger = nenofex->graph_root->size_subformula +
                (nenofex->graph_root->size_subformula *
                 nenofex->options.univ_trigger_delta / 100);
              if (nenofex->options.show_progress_specified)
                fprintf (stderr, "New univ-trigger-abs: %d\n",
                         universal_trigger);
#endif
            }
        }
      else
        {
          assert (nenofex->consider_univ_exp == 0);
        }

      /* check if size-cutoff occurred */
      if (size_cutoff_specified)
        {
          if (size_cutoff_relative_specified)
            {
              if (size_after_expansion >
                  (size_before_expansion *
                   (1 + nenofex->options.size_cutoff)))
                {
                  fprintf (stderr,
                           "\n\tSIZE CUTOFF: cutoff = %f, before = %d, after = %d\n\n",
                           nenofex->options.size_cutoff,
                           size_before_expansion, size_after_expansion);
                  break;
                }
            }
          else                  /* absolute cutoff */
            {
              if (size_after_expansion >
                  (size_before_expansion + nenofex->options.size_cutoff))
                {
                  fprintf (stderr,
                           "\n\tSIZE CUTOFF: cutoff = %f, size before = %d, size after = %d\n\n",
                           nenofex->options.size_cutoff,
                           size_before_expansion, size_after_expansion);
                  break;
                }
            }
        }                       /* end: size_cutoff specified */

#ifndef NDEBUG
#if ASSERT_AFTER_EXP_ALL_NON_INNERMOST_SCOPE_VARS_UNINIT
      assert_all_non_innermost_scope_vars_uninitialized (nenofex,
                                                         nenofex->cur_scope);
#endif
#endif

    }                           /* end: elimination-while */

  nenofex->expansion_phase_end_time = time_stamp ();
}


static void
sat_solving_phase (Nenofex * nenofex)
{
  int dump = nenofex->options.dump_cnf_specified;
  int forward = !nenofex->options.no_sat_solving_specified;
  assert (dump || forward);

  if (nenofex->result == NENOFEX_RESULT_UNKNOWN
      && !nenofex->sat_solver_tautology_mode)
    {
      assert (is_formula_existential (nenofex));

      if (forward)
        {
          sat_solver_init ();
          if (nenofex->options.verbose_sat_solving_specified)
            sat_solver_verbosity_mode ();
          nnf_to_cnf_forward (nenofex);
        }

      if (dump)                 /* note: cnf is generated from scratch if this option is toggled */
        nnf_to_cnf_dump (nenofex, stdout);

      if (forward)
        {
          int sat_res = sat_solver_sat ();
          assert (sat_res);

          nenofex->result = sat_res;

          if (nenofex->options.print_assignment_specified &&
              nenofex->result == NENOFEX_RESULT_SAT)
            {
              get_var_assignments_from_sat_solver (nenofex);
              generate_qdimacs_output (nenofex, stdout);
            }

          sat_solver_reset ();
        }
    }                           /* end: propositional formula */


  if (nenofex->result == NENOFEX_RESULT_UNKNOWN
      && nenofex->sat_solver_tautology_mode)
    {
      assert (is_formula_universal (nenofex));

      if (forward)
        {
          sat_solver_init ();
          if (nenofex->options.verbose_sat_solving_specified)
            sat_solver_verbosity_mode ();
          nnf_to_cnf_forward (nenofex);
        }

      if (dump)                 /* note: cnf is generated from scratch if this option is toggled */
        nnf_to_cnf_dump (nenofex, stdout);

      if (forward)
        {
          int sat_res = sat_solver_sat ();
          assert (sat_res);

          nenofex->result = sat_res == SAT_SOLVER_RESULT_SATISFIABLE ?
            NENOFEX_RESULT_UNSAT : NENOFEX_RESULT_SAT;

          if (nenofex->options.print_assignment_specified &&
              nenofex->result == NENOFEX_RESULT_UNSAT)
            {                   /* implies sat-solver returned SAT */
              get_var_assignments_from_sat_solver (nenofex);
              generate_qdimacs_output (nenofex, stdout);
            }

          sat_solver_reset ();
        }
    }                           /* end: universal formula */
}


static int
count_scope_variables (Nenofex * nenofex, Scope * scope)
{
  int result = 0;

  void **v_var, **var_end;
  var_end = scope->vars->top;
  for (v_var = scope->vars->elems; v_var < var_end; v_var++)
    {
      Var *var = *v_var;
      if (variable_has_occs (var))
        result++;
    }

  return result;
}


static int
count_variables (Nenofex * nenofex, const int count_existential)
{
  int result = 0;

  void **v_scope, **scope_end;
  scope_end = nenofex->scopes->top;
  for (v_scope = nenofex->scopes->elems; v_scope < scope_end; v_scope++)
    {
      Scope *scope = *v_scope;
      if (count_existential && is_existential_scope (scope))
        result += count_scope_variables (nenofex, scope);
      else if (!count_existential && is_universal_scope (scope))
        result += count_scope_variables (nenofex, scope);
    }

  return result;
}


static void
print_statistics_at_start (Nenofex * nenofex)
{
  int graph_size =
    nenofex->graph_root ? nenofex->graph_root->size_subformula : 0;
  int num_clauses_specified = nenofex->num_orig_clauses;
  int num_vars_specified = nenofex->num_orig_vars;
  int num_exist_vars = count_variables (nenofex, 1);
  int num_univ_vars = count_variables (nenofex, 0);

  fprintf (stderr, "\nEntering expansion phase:\n");
  fprintf (stderr, "  tree size: %d\n", graph_size);
  fprintf (stderr, "  specified number of variables: %d (E: %d, A: %d)\n",
           num_vars_specified, num_exist_vars, num_univ_vars);
  fprintf (stderr, "  specified number of clauses: %d (current: %d)\n",
           num_clauses_specified,
           nnf_to_cnf_tseitin_revised_count_clauses (nenofex));
}


static void
print_statistics_after_expansion (Nenofex * nenofex)
{
  int graph_size =
    nenofex->graph_root ? nenofex->graph_root->size_subformula : 0;
  int num_exist_vars = count_variables (nenofex, 1);
  int num_univ_vars = count_variables (nenofex, 0);

  fprintf (stderr, "\nAfter expansion phase:\n");
  fprintf (stderr, "  tree size: %d\n", graph_size);
#if COMPUTE_MAX_TREE_SIZE
  fprintf (stderr, "  max. tree size: %d\n", nenofex->stats.max_tree_size);
#endif
#if COMPUTE_NUM_UNITS
  fprintf (stderr, "  units: %d\n", nenofex->stats.num_units);
#endif
#if COMPUTE_NUM_UNATES
  fprintf (stderr, "  unates: %d\n", nenofex->stats.num_unates);
#endif
  fprintf (stderr, "  expansions: %d\n", nenofex->cur_expansions);
#if COMPUTE_CASES_IN_EXPANSIONS
  fprintf (stderr, "    case [E,OR,=]: %d\n",
           nenofex->stats.num_exp_case_E_OR_ALL);
  fprintf (stderr, "    case [E,OR,<]: %d\n",
           nenofex->stats.num_exp_case_E_OR_SUBSET);
  fprintf (stderr, "    case [E,AND,=]: %d\n",
           nenofex->stats.num_exp_case_E_AND_ALL);
  fprintf (stderr, "    case [E,AND,<]: %d\n",
           nenofex->stats.num_exp_case_E_AND_SUBSET);
  fprintf (stderr, "    case [A,AND,=]: %d\n",
           nenofex->stats.num_exp_case_A_AND_ALL);
  fprintf (stderr, "    case [A,AND,<]: %d\n",
           nenofex->stats.num_exp_case_A_AND_SUBSET);
  fprintf (stderr, "    case [A,OR,=]: %d\n",
           nenofex->stats.num_exp_case_A_OR_ALL);
  fprintf (stderr, "    case [A,OR,<]: %d\n",
           nenofex->stats.num_exp_case_A_OR_SUBSET);
#endif
#if COMPUTE_NUM_NON_INC_EXPANSIONS_IN_SCORES
  fprintf (stderr, "  possible non-inc. expansions: %d\n",
           nenofex->stats.num_non_inc_expansions_in_scores);
#endif
#if COMPUTE_NUM_NON_INC_EXPANSIONS
  fprintf (stderr, "  actual non-inc. expansions: %d\n",
           nenofex->stats.num_non_inc_expansions);
#endif
  fprintf (stderr, "  expansion time: %.2fs\n",
           nenofex->expansion_phase_end_time - nenofex->start_time);
#if COMPUTE_AVERAGE_UPDATE_MARKED_RATIOS
  if (nenofex->cur_expansions)
    {
      fprintf (stderr, "  avg. ratio lca-marked / remaining scope vars: %f\n",
               nenofex->stats.sum_ratio_lca_marked_in_scope_vars /
               nenofex->cur_expansions);
      fprintf (stderr, "  avg. ratio inc-marked / remaining scope vars: %f\n",
               nenofex->stats.sum_ratio_inc_marked_in_scope_vars /
               nenofex->cur_expansions);
      fprintf (stderr, "  avg. ratio dec-marked / remaining scope vars: %f\n",
               nenofex->stats.sum_ratio_dec_marked_in_scope_vars /
               nenofex->cur_expansions);
      double avg_remaining =
        (double) nenofex->stats.sum_remaining / nenofex->cur_expansions;
      fprintf (stderr, "  avg. remaining scope vars %f\n", avg_remaining);
      fprintf (stderr, "  avg. lca-marked in remaining scope vars: %f\n",
               (double) nenofex->stats.sum_lca_marked /
               nenofex->cur_expansions);
      fprintf (stderr, "  avg. inc-marked in remaining scope vars: %f\n",
               (double) nenofex->stats.sum_inc_marked /
               nenofex->cur_expansions);
      fprintf (stderr, "  avg. dec-marked in remaining scope vars: %f\n",
               (double) nenofex->stats.sum_dec_marked /
               nenofex->cur_expansions);
    }
#endif
#if COMPUTE_LCA_PARENT_VISITS
  if (nenofex->stats.num_total_lca_algo_calls)
    fprintf (stderr,
             "  avg. parent visits in LCA computation: %f (total %d calls)\n",
             (double) nenofex->stats.num_total_lca_parent_visits /
             nenofex->stats.num_total_lca_algo_calls,
             nenofex->stats.num_total_lca_algo_calls);
#endif
  fprintf (stderr, "  optimizations called: %d\n",
           nenofex->performed_optimizations);
  fprintf (stderr, "  optimizations succeeded: %d\n",
           nenofex->successful_optimizations);
  fprintf (stderr, "  first successful optimizations after %d expansions\n",
           nenofex->first_successful_opt);
  if (nenofex->options.post_expansion_flattening_specified)
    fprintf (stderr, "  post-expansion flattening applied: %d\n",
             nenofex->cnt_post_expansion_flattenings);
  fprintf (stderr, "  remaining orig. variables: %d (E: %d, A: %d)\n",
           num_exist_vars + num_univ_vars, num_exist_vars, num_univ_vars);
#if COMPUTE_NUM_TOTAL_CREATED_NODES
  fprintf (stderr, "  total created nodes: %d\n",
           nenofex->stats.num_total_created_nodes);
#endif

#if COMPUTE_NUM_DELETED_NODES
  fprintf (stderr,
           "  total deleted nodes: %d with %.3f%% (%d) in optimizations\n",
           nenofex->stats.total_deleted_nodes,
           nenofex->stats.deleted_nodes_by_global_flow_redundancy /
           (float) nenofex->stats.total_deleted_nodes * 100,
           nenofex->stats.deleted_nodes_by_global_flow_redundancy);
#endif
}


static int
solve (Nenofex * nenofex)
{
  if (!nenofex->options.print_short_answer_specified)
    print_statistics_at_start (nenofex);

  if (nenofex->result != NENOFEX_RESULT_UNKNOWN)
    {
      assert (!nenofex->graph_root);
      return nenofex->result;
    }

  expansion_phase (nenofex);

  if (!nenofex->options.print_short_answer_specified)
    print_statistics_after_expansion (nenofex);

  nenofex->is_existential = is_formula_existential (nenofex);
  nenofex->is_universal = is_formula_universal (nenofex);

  if (nenofex->result == NENOFEX_RESULT_UNKNOWN &&
      (nenofex->is_existential || nenofex->is_universal) &&
      (!nenofex->options.no_sat_solving_specified
       || nenofex->options.dump_cnf_specified))
    {
      sat_solving_phase (nenofex);
    }
  else if (nenofex->result == NENOFEX_RESULT_UNKNOWN
           && nenofex->options.dump_cnf_specified)
    {
      /* Dump quantified NNF. */
      quantified_nnf_to_cnf_dump (nenofex, stdout);
    }

  return nenofex->result;
}


int
nenofex_main (int argc, char **argv)
{
  int done = 0;
  int result = NENOFEX_RESULT_UNKNOWN;

  Nenofex *nenofex = create_nenofex ();

  nenofex->start_time = time_stamp ();

  /* default settings */
  set_default_cmd_line_options (nenofex);

  done = parse_cmd_line_options (nenofex, argc, argv);

  if (nenofex->options.max_time)
    {
      alarm (nenofex->options.max_time);
      if (!nenofex->options.print_short_answer_specified)
        fprintf (stderr, "Time limit set to %u seconds\n",
                 nenofex->options.max_time);
    }

  if (done)
    goto FREE_GRAPH;

#ifndef NDEBUG
  assert_solver_options (nenofex);
#endif

  DIR *dir = 0;
  FILE *input_file = 0;

  if (!nenofex->options.input_filename)
    input_file = stdin;
  else
    {
      if ((dir = opendir (nenofex->options.input_filename)) != NULL)
        {
          fprintf (stderr, "'%s' is a directory!\n\n",
                   nenofex->options.input_filename);
          closedir (dir);
          exit (1);
        }
      input_file = fopen (nenofex->options.input_filename, "r");
    }

  if (!done && input_file)
    done = parse (nenofex, input_file);
  else if (!done)
    {
      fprintf (stderr, "Could not open file '%s'!\n\n",
               nenofex->options.input_filename);
      exit (1);
    }

  if (nenofex->options.input_filename)
    fclose (input_file);

#ifndef NDEBUG
#if ASSERT_GRAPH_AFTER_PARSING
  assert_all_child_occ_lists_integrity (nenofex);
  assert_all_occ_lists_integrity (nenofex);
  assert_all_subformula_sizes (nenofex);
  assert_all_one_level_simplified (nenofex);
#endif
#endif

  if (!done)
    {
      solve (nenofex);
    }

  result = nenofex->result;

  if (!nenofex->options.print_short_answer_specified)
    {
      fprintf (stderr, "\n\tresult:\t%s after %d expansions\n",
               nenofex->result == NENOFEX_RESULT_SAT ?
               "TRUE" : (nenofex->result ==
                         NENOFEX_RESULT_UNSAT ? "FALSE" : "UNKNOWN"),
               nenofex->cur_expansions);
    }
  else
    {
      int answer;
      if (nenofex->result == NENOFEX_RESULT_SAT)
        answer = 1;
      else if (nenofex->result == NENOFEX_RESULT_UNSAT)
        answer = 0;
      else
        answer = -1;

      fprintf (stdout, "s cnf %d %d %d\n", answer,
               nenofex->num_orig_vars, nenofex->num_orig_clauses);
    }

  nenofex->end_time = time_stamp ();

  double total_time = nenofex->end_time - nenofex->start_time;
  double expansion_time =
    nenofex->expansion_phase_end_time - nenofex->start_time;

  if (expansion_time < 0)
    expansion_time = 0;

  if (!nenofex->options.print_short_answer_specified)
    fprintf (stderr, "\ttime:\t%.2fs with %.2fs in expansions\n",
             total_time, expansion_time);

FREE_GRAPH:
  free_nenofex (nenofex);

  mem_check ();

  return result;
}
