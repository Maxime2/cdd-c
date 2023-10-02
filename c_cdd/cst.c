#include <assert.h>
#include <ctype.h>

#include "c_cdd_utils.h"
#include "cst.h"
#include "cst_parser_helpers.h"
#include "str_includes.h"
#include "tokenizer_helpers.h"
#include "tokenizer_types.h"

struct TokenizerVars {
  ssize_t c_comment_char_at, cpp_comment_char_at, line_continuation_at;
  uint32_t spaces;
  uint32_t lparen, rparen, lsquare, rsquare, lbrace, rbrace, lchev, rchev;
  bool in_c_comment, in_cpp_comment, in_single, in_double, in_macro, in_init,
      is_digit;
};

struct TokenizerVars *clear_sv(struct TokenizerVars *);

int tokenizer(const az_span source,
              struct tokenizer_az_span_arr *const *const tokens_arr) {
  size_t i;
  const size_t source_n = az_span_size(source);
  const uint8_t *const span_ptr = az_span_ptr(source);

  /* Allocation approach:
   *   0. Before loop: over allocate; maximum tokens == number of chars
   *   1. After loop: reduce to hold number of actual tokens created
   * */

  (**tokens_arr).size = 0,
  (**tokens_arr).elem = malloc(source_n * sizeof *(**tokens_arr).elem);
  if ((**tokens_arr).elem == NULL) {
    return ENOMEM;
  }

  /* Tokenizer algorithm:
   *   0. Use last 2 chars—3 on comments—to determine type;
   *   1. Pass to type-eating function, returning whence finished reading;
   *   2. Repeat from 0. until end of `source`.
   *
   * Tokenizer types (line-continuation aware):
   *   - whitespace   [ \t\v\n]+
   *   - macro        [if|ifdef|ifndef|elif|endif|include|define]
   *   - terminator   ;
   *   - parentheses  {}[]()
   *   - word         space or comment separated that doesn't match^
   */

  for (i = 0; i < source_n; i++) {
    const uint8_t next_ch = span_ptr[i + 1], ch = span_ptr[i],
                  last_ch = span_ptr[i - 1] /* NUL when i=0 */;
    bool handled = false;
    /* printf("i                                 = %ld\n", i); */

    if (last_ch == '/' && (i < 2 || span_ptr[i - 2] != '\\')) {
      /* Handle comments */
      switch (ch) {
      case '*':
        i = eatCComment(&source, i - 1, source_n,
                        (**tokens_arr).elem + (**tokens_arr).size++),
        handled = true;
        break;

      case '/':
        if (span_ptr[i - 2] != '*') {
          /* ^ handle consecutive C-style comments `/\*bar*\/\*foo*\/` */
          i = eatCppComment(&source, i - 1, source_n,
                            (**tokens_arr).elem + (**tokens_arr).size++),
          handled = true;
        }
        break;

      default:
        break;
      }
    }

    if (!handled && last_ch != '\\')
      switch (ch) {
      /* Handle macros */
      case '#':
        i = eatMacro(&source, i, source_n,
                     (**tokens_arr).elem + (**tokens_arr).size++);
        break;

      /* Handle single-quoted char literal */
      case '\'':
        i = eatCharLiteral(&source, i, source_n,
                           (**tokens_arr).elem + (**tokens_arr).size++);
        break;

      /* Handle double-quoted string literal */
      case '"':
        i = eatStrLiteral(&source, i, source_n,
                          (**tokens_arr).elem + (**tokens_arr).size++);
        break;

      case ' ':
      case '\n':
      case '\r':
      case '\t':
      case '\v':
        i = eatWhitespace(&source, i, source_n,
                          (**tokens_arr).elem + (**tokens_arr).size++);
        break;

      case '{':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   LBRACE);
        break;

      case '}':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   RBRACE);
        break;

      case '[':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   LSQUARE);
        break;

      case ']':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   RSQUARE);
        break;

      case '(':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   LPAREN);
        break;

      case ')':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   RPAREN);
        break;

      case ';':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   TERMINATOR);
        break;

      /* parser not tokenizer will determine if ternary operator or label */
      case ':':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   COLON);
        break;

      case '?':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   QUESTION);
        break;

      case '~':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   TILDE);
        break;

      case '!':
        if (next_ch == '=')
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, NE_OP);
        else
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     EXCLAMATION);
        break;

      case ',':
        eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                   COMMA);
        break;

      case '.':
        if (isdigit(next_ch))
          i = eatNumber(&source, i, source_n,
                        (**tokens_arr).elem + (**tokens_arr).size++);
        else if (next_ch == '.' && span_ptr[i + 2] == '.')
          i = eatSlice(&source, i, 3,
                       (**tokens_arr).elem + (**tokens_arr).size++, ELLIPSIS);
        break;

      case '>':
        switch (next_ch) {
        case '>':
          if (span_ptr[i + 2] == '=')
            i = eatSlice(&source, i, 3,
                         (**tokens_arr).elem + (**tokens_arr).size++,
                         RIGHT_ASSIGN);
          else
            i = eatSlice(&source, i, 2,
                         (**tokens_arr).elem + (**tokens_arr).size++,
                         RIGHT_SHIFT);
          break;
        case '=':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, GE_OP);
          break;
        default:
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     GREATER_THAN);
          break;
        }
        break;

      case '<':
        switch (next_ch) {
        case '<':
          if (span_ptr[i + 2] == '=')
            i = eatSlice(&source, i, 3,
                         (**tokens_arr).elem + (**tokens_arr).size++,
                         LEFT_ASSIGN);
          else
            i = eatSlice(&source, i, 2,
                         (**tokens_arr).elem + (**tokens_arr).size++,
                         LEFT_SHIFT);
          break;
        case '=':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, LE_OP);
          break;
        default:
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     LESS_THAN);
          break;
        }
        break;

      case '+':
        switch (next_ch) {
        case '+':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, INC_OP);
          break;
        case '=':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, ADD_ASSIGN);
          break;
        default:
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     PLUS);
          break;
        }
        break;

      case '-':
        switch (next_ch) {
        case '-':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, DEC_OP);
          break;
        case '=':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, SUB_ASSIGN);
          break;
        case '>':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, PTR_OP);
          break;
        default:
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     SUB);
          break;
        }
        break;

      case '*':
        if (next_ch == '=')
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, MUL_ASSIGN);
        else
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     ASTERISK);
        break;

      case '/':
        switch (next_ch) {
        case '=':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, MUL_ASSIGN);
          break;
        case '/':
        case '*':
          break;
        default:
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     DIVIDE);
        }
        break;

      case '%':
        if (next_ch == '=')
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, MODULO);
        else
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     MOD_ASSIGN);
        break;

      case '&':
        switch (next_ch) {
        case '&':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, AND_OP);
          break;
        case '=':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, AND_ASSIGN);
          break;
        default:
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     AND);
          break;
        }
        break;

      case '^':
        if (next_ch == '=')
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, XOR_ASSIGN);
        else
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     CARET);
        break;

      case '|':
        switch (next_ch) {
        case '|':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, OR_OP);
          break;
        case '=':
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, OR_ASSIGN);
          break;
        default:
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     PIPE);
          break;
        }
        break;

      case '=':
        if (next_ch == '=')
          i = eatSlice(&source, i, 2,
                       (**tokens_arr).elem + (**tokens_arr).size++, EQ_OP);
        else
          eatOneChar(&source, i, (**tokens_arr).elem + (**tokens_arr).size++,
                     EQUAL);
        break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        /* Misses numbers with a sign [+-] */
        i = eatNumber(&source, i, source_n,
                      (**tokens_arr).elem + (**tokens_arr).size++);
        break;

      case '_':
      case 'a':
      case 'b':
      case 'c':
      case 'd':
      case 'e':
      case 'f':
      case 'g':
      case 'h':
      case 'i':
      case 'j':
      case 'k':
      case 'l':
      case 'm':
      case 'n':
      case 'o':
      case 'p':
      case 'q':
      case 'r':
      case 's':
      case 't':
      case 'u':
      case 'v':
      case 'w':
      case 'x':
      case 'y':
      case 'z':
      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
      case 'G':
      case 'H':
      case 'I':
      case 'J':
      case 'K':
      case 'L':
      case 'M':
      case 'N':
      case 'O':
      case 'P':
      case 'Q':
      case 'R':
      case 'S':
      case 'T':
      case 'U':
      case 'V':
      case 'W':
      case 'X':
      case 'Y':
      case 'Z':
        i = eatWord(&source, i, source_n,
                    (**tokens_arr).elem + (**tokens_arr).size++);
        break;

      default:
        break;
      }
  }

  /*az_span_list_push_valid(&source, ll, i, &sv, &tokenized_cur_ptr,
   * &start_index);*/

  /*printf("tokenized_ll: {kind=%s};\n",
  TokenizerKind_to_str(tokenized_ll->kind)); print_escaped_span("tokenized_ll",
  tokenized_ll->span);

  printf("tokenized_ll->next: {kind=%s};\n",
  TokenizerKind_to_str(tokenized_ll->next->kind));
  print_escaped_span("tokenized_ll->next", tokenized_ll->next->span);
  tokenized_ll->next = NULL;
  printf("tokenized_ll->next: %s;\n", tokenized_ll->next);*/

  (**tokens_arr).elem =
      realloc((**tokens_arr).elem,
              ((**tokens_arr).size + 1) * sizeof *(**tokens_arr).elem);
  if ((**tokens_arr).elem == NULL)
    return ENOMEM;

  /* For iterating without using offset; also explains the `+1` in `realloc` */
  (**tokens_arr).elem[(**tokens_arr).size].span._internal.ptr = NULL,
  (**tokens_arr).elem[(**tokens_arr).size].kind = UNKNOWN_SCAN;

  return EXIT_SUCCESS;
}

struct TokenizerVars *clear_sv(struct TokenizerVars *sv) {
  sv->c_comment_char_at = 0, sv->cpp_comment_char_at = 0,
  sv->line_continuation_at = 0;
  sv->spaces = 0;
  sv->lparen = 0, sv->rparen = 0, sv->lsquare = 0, sv->rsquare = 0,
  sv->lbrace = 0, sv->rbrace = 0, sv->lchev = 0, sv->rchev = 0;
  sv->in_c_comment = false, sv->in_cpp_comment = false, sv->in_single = false,
  sv->in_double = false, sv->in_macro = false, sv->in_init = false;
  return sv;
}

az_span make_slice_clear_vars(const az_span source, size_t i,
                              size_t *start_index, struct TokenizerVars *sv,
                              bool always_make_expr) {
  if (always_make_expr ||
      (!sv->in_single && !sv->in_double && !sv->in_c_comment &&
       !sv->in_cpp_comment && sv->line_continuation_at != i - 1 &&
       sv->lparen == sv->rparen && sv->lsquare == sv->rsquare &&
       sv->lchev == sv->rchev)) {
    const az_span slice = az_span_slice(source, *start_index, i);
    clear_sv(sv);
    *start_index = i;
    return slice;
  }
  return az_span_empty();
}

struct CstParseVars {
  bool is_union, is_struct, is_enum, is_function;
  size_t lparens, rparens, lbraces, rbraces, lsquare, rsquare;
};

void clear_CstParseVars(struct CstParseVars *pv) {
  pv->is_enum = false, pv->is_union = false, pv->is_struct = false;
  pv->lparens = 0, pv->rparens = 0, pv->lbraces = 0, pv->rbraces = 0,
  pv->lsquare = 0, pv->rsquare = 0;
}

int cst_parser(const struct tokenizer_az_span_arr *const tokens_arr,
               struct cst_node_arr **cst_arr) {
  /* recognise start/end of function, struct, enum, union */

  /*struct parse_cst_elem *parse_ll = NULL;*/
  /*struct parse_cst_elem **tokenized_cur_ptr = &tokenized_ll;*/

  /*struct CstNode *ll = malloc((sizeof (ll)));
  struct tokenizer_az_span_elem *iter;*/

  size_t i, parse_start;

  struct CstParseVars vars = {false, false, false, false};

  /*enum TokenizerKind expression[] = {TERMINATOR};
  enum TokenizerKind function_start[] = {
      WORD / * | Keyword* /, WHITESPACE | C_COMMENT | CPP_COMMENT, LPAREN,
      WORD / * | Keyword* /, WHITESPACE | C_COMMENT | CPP_COMMENT, RPAREN,
      WORD / * | Keyword* /, WHITESPACE | C_COMMENT | CPP_COMMENT, LBRACE};*/

  /*enum TokenizerKind *tokenizer_kinds[] = {
      {LBRACE, MUL_ASSIGN},
      {LBRACE, MUL_ASSIGN},
  };*/

  /* putchar('\n'); */

  tokenizer_az_span_arr_print(tokens_arr);

  putchar('\n');

  for (i = 0, parse_start = 0; i < tokens_arr->size; i++) {
    const struct tokenizer_az_span_elem *const tok_span_el =
        &tokens_arr->elem[i];
    switch (tok_span_el->kind) {
    /*`
    for (iter = (struct tokenizer_az_span_elem *)tokens_ll->list, i = 0;
         iter != NULL; iter = iter->next, i++) {
      switch (iter->kind) */
    case C_COMMENT:
    case CPP_COMMENT:
    case WHITESPACE:
      break;

    case enumKeyword:
      vars.is_union = false;
      break;

    case unionKeyword:
      vars.is_enum = false;
      break;

    case WORD:
      /* can still be `enum` or `union` at this point */
      break;

    case ASTERISK:
    case _AlignasKeyword:
    case _AlignofKeyword:
    case _AtomicKeyword:
    case _BitIntKeyword:
    case _BoolKeyword:
    case _ComplexKeyword:
    case _Decimal128Keyword:
    case _Decimal32Keyword:
    case _Decimal64Keyword:
    case _NoreturnKeyword:
    case alignasKeyword:
    case alignofKeyword:
    case autoKeyword:
    case boolKeyword:
    case charKeyword:
    case constKeyword:
    case doubleKeyword:
    case externKeyword:
    case floatKeyword:
    case inlineKeyword:
    case intKeyword:
    case longKeyword:
    case shortKeyword:
    case signedKeyword:
    case staticKeyword:
    case unsignedKeyword:
    case voidKeyword:
    case volatileKeyword:
      vars.is_enum = false, vars.is_union = false;
      break;

    case structKeyword:
      vars.is_enum = false, vars.is_union = false, vars.is_struct = true;
      break;

      /* can still be struct, enum, union, function here */
    case TERMINATOR: {
      size_t j;
      clear_CstParseVars(&vars);

      puts("<EXPRESSION>");
      for (j = parse_start; j < i; j++) {
        print_escaped_span(TokenizerKind_to_str(tokens_arr->elem[j].kind),
                           tokens_arr->elem[j].span);
      }
      puts("</EXPRESSION>");
      parse_start = i;
      break;
    }

    case LBRACE:
      vars.lbraces++;

      if (vars.lparens == vars.rparens && vars.lsquare == vars.rsquare) {
        if (!vars.is_enum && !vars.is_union && !vars.is_struct &&
            vars.lparens > 0 && vars.lparens == vars.rparens)
          i = eatFunction(tokens_arr, parse_start, i) - 1;
        else if (vars.is_enum && !vars.is_union && !vars.is_struct)
          /* could be an anonymous enum at the start of a function def */
          puts("WITHIN ENUM");
        else if (!vars.is_enum && vars.is_union && !vars.is_struct)
          puts("WITHIN UNION");
        else if (!vars.is_enum && !vars.is_union && vars.is_struct)
          puts("WITHIN STRUCT");

        clear_CstParseVars(&vars);
      }

      /*
      tokenizer_az_span_elem_arr_push(&ll->size, &tokenized_cur_ptr, iter->kind,
      iter->span); print_tokenizer_az_span_elem_arr(token_ll, i); token_ll->list
      = tokenized_ll; tokenizer_az_span_elem_arr_cleanup(token_ll);
      */
      break;

    case RBRACE:
      vars.rbraces++;
      /* TODO: Handle initializer and nested initializer lists */
      if (vars.lparens == vars.rparens && vars.lsquare == vars.rsquare &&
          vars.lbraces == vars.rbraces) {
        size_t j;

        char *parse_kind;
        if (vars.is_function)
          parse_kind = "FUNCTION";
        else if (vars.is_enum && !vars.is_union && !vars.is_struct)
          parse_kind = "ENUM";
        else if (!vars.is_enum && vars.is_union && !vars.is_struct)
          parse_kind = "UNION";
        else if (!vars.is_enum && !vars.is_union && vars.is_struct)
          parse_kind = "STRUCT";
        else
          parse_kind = "UNKNOWN";
        printf("<%s>\n", parse_kind);
        for (j = parse_start; j < i; j++) {
          print_escaped_span(TokenizerKind_to_str(tokens_arr->elem[j].kind),
                             tokens_arr->elem[j].span);
        }
        printf("</%s>\n", parse_kind);

        clear_CstParseVars(&vars);
        parse_start = i;
      }
      break;

    case LPAREN:
      vars.lparens++;
      break;

    case RPAREN:
      vars.rparens++;
      break;

    case LSQUARE:
      vars.lsquare++;
      break;

    case RSQUARE:
      vars.rsquare++;
      break;

    case ADD_ASSIGN:
    case AND:
    case AND_ASSIGN:
    case AND_OP:
    case CARET:
    case COLON:
    case COMMA:
    case DEC_OP:
    case DIVIDE:
    case DIV_ASSIGN:
    case DOUBLE_QUOTED:
    case ELLIPSIS:
    case EQUAL:
    case EQ_OP:
    case EXCLAMATION:
    case GE_OP:
    case GREATER_THAN:
    case INC_OP:
    case LEFT_ASSIGN:
    case LEFT_SHIFT:
    case LESS_THAN:
    case LE_OP:
    case MACRO:
    case MODULO:
    case MOD_ASSIGN:
    case MUL_ASSIGN:
    case NE_OP:
    case OR_ASSIGN:
    case OR_OP:
    case PIPE:
    case PLUS:
    case PTR_OP:
    case QUESTION:
    case RIGHT_ASSIGN:
    case RIGHT_SHIFT:
    case SINGLE_QUOTED:
    case SUB:
    case SUB_ASSIGN:
    case TILDE:
    case UNKNOWN_SCAN:
    case XOR_ASSIGN:
    case _GenericKeyword:
    case _ImaginaryKeyword:
    case _Static_assertKeyword:
    case _Thread_localKeyword:
    case breakKeyword:
    case caseKeyword:
    case constexprKeyword:
    case continueKeyword:
    case defaultKeyword:
    case doKeyword:
    case elseKeyword:
    case falseKeyword:
    case forKeyword:
    case gotoKeyword:
    case ifKeyword:
    case nullptrKeyword:
    case registerKeyword:
    case restrictKeyword:
    case returnKeyword:
    case sizeofKeyword:
    case static_assertKeyword:
    case switchKeyword:
    case thread_localKeyword:
    case trueKeyword:
    case typedefKeyword:
    case typeofKeyword:
    case typeof_unqualKeyword:
    case whileKeyword:
    default:
      puts("<DEFAULT>");
      print_escaped_span(TokenizerKind_to_str(tok_span_el->kind),
                         tok_span_el->span);
      puts("</DEFAULT>");
      break;
    }
  }
  puts("*******************");
  /*token_ll->list = tokenized_ll;*/

  /*tokenizer_az_span_elem_arr_cleanup(token_ll);*/

  return EXIT_SUCCESS;
}
