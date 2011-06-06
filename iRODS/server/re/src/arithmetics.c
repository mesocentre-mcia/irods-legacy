/* For copyright information please refer to files in the COPYRIGHT directory
 */

#include "utils.h"
#include "parser.h"
#include "arithmetics.h"
#include "datetime.h"
#include "index.h"
#include "rules.h"
#include "functions.h"
#include "configuration.h"

#ifndef DEBUG
#include "regExpMatch.h"
#include "rodsErrorTable.h"
typedef struct {
  char action[MAX_ACTION_SIZE];
  int numberOfStringArgs;
  funcPtr callAction;
} microsdef_t;
extern int NumOfAction;
extern microsdef_t MicrosTable[];
#endif

#define ERROR(x, y) if(x) {if((y)!=NULL){(y)->type.t=ERROR;*errnode=node;}return;}
#define OUTOFMEMORY(x, res) if(x) {(res)->value.e = OUT_OF_MEMORY;TYPE(res) = ERROR;return;}

#define ERROR2(x,y) if(x) {localErrorMsg=(y);goto error;}
extern int GlobalAllRuleExecFlag;
extern int GlobalREDebugFlag;

/* utilities */
Env* globalEnv(Env *env) {
        Env *global = env;
        while(global->previous!=NULL) {
            global = global->previous;
        }
        return global;
}

int initializeEnv(Node *params, Res *args[MAX_NUM_OF_ARGS_IN_ACTION], int argc, Hashtable *env, Region *r) {


	Node** args2 = params->subtrees;
/*	int argc2 = ruleHead->degree; */
	int i;
        /*getSystemFunctions(env, r); */
	for (i = 0; i < argc ; i++) {
		insertIntoHashTable(env, args2[i]->text, args[i]);
	}
	return (0);
}

char *matchWholeString(char *buf) {
    char *buf2 = (char *)malloc(sizeof(char)*strlen(buf)+2+1);
    buf2[0]='^';
    strcpy(buf2+1, buf);
    buf2[strlen(buf)+1]='$';
    buf2[strlen(buf)+2]='\0';
    return buf2;
}

char* getVariableName(Node *node) {
    return node->subtrees[0]->text;
}


Res* evaluateExpression3(Node *expr, int applyAll, int force, ruleExecInfo_t *rei, int reiSaveFlag, Env *env, rError_t* errmsg, Region *r) {
/*
    printTree(expr, 0);
*/
    char errbuf[ERR_MSG_LEN];
    Res *res = newRes(r), *funcRes = NULL, *argRes = NULL;
    FunctionDesc *fd = NULL;
    int i;
    Res **tupleComps = NULL;
    if(force || expr->iotype == IO_TYPE_INPUT) {
		switch(expr->nodeType) {
			case TK_INT:
				res->exprType = newSimpType(T_INT,r);
				res->value.dval=atof(expr->text);
							break;
			case TK_DOUBLE:
				res->exprType = newSimpType(T_DOUBLE,r);
				res->value.dval=atof(expr->text);
							break;
			case TK_STRING:
				res = newStringRes(r, expr->text);
							break;
			case TK_VAR:
					res = evaluateVar3(expr->text, expr, rei, reiSaveFlag,  env, errmsg,r);
					break;
			case TK_TEXT:
					fd = (FunctionDesc *)lookupFromHashTable(ruleEngineConfig.funcDescIndex, expr->text);
					if(fd!=NULL) {
						int nArgs = 0;
						ExprType *type = fd->exprType;
						while(type->nodeType == T_CONS && strcmp(type->text, FUNC) == 0) {
							type = type->subtrees[1];
							nArgs ++;
						}
						if(nArgs == 0) {
							Node *appNode = newPartialApplication(expr, newTupleRes(0,NULL,r), 0, r);
							res = evaluateFunction3(appNode, applyAll, expr, env, rei, reiSaveFlag, errmsg, r);
						} else {
							res = newFuncSymLink(expr->text, nArgs, r);
						}
					} else {
						res = newFuncSymLink(expr->text, 1, r);
					}
					break;


			case N_APPLICATION:
							/* try to evaluate as a function, */
	/*
							printf("start execing %s\n", oper1);
							printEnvToStdOut(env);

	*/			funcRes = evaluateExpression3(expr->subtrees[0], applyAll, 0, rei, reiSaveFlag, env, errmsg,r);
				if(funcRes->nodeType==N_ERROR) {
					res = funcRes;
					break;
				}
				/* printTree(expr->subtrees[1], 0); */
				argRes = evaluateExpression3(expr->subtrees[1], applyAll, 0, rei, reiSaveFlag, env, errmsg,r);
				if(argRes->nodeType==N_ERROR) {
					res = argRes;
					break;
				}
				res = evaluateFunctionApplication(funcRes, argRes, applyAll, expr, rei, reiSaveFlag, env, errmsg,r);
	/*
							printf("finish execing %s\n", oper1);
							printEnvToStdOut(env);
	*/
							break;
			case N_TUPLE:
				tupleComps = (Res **) region_alloc(r, sizeof(Res *) * expr->degree);
				for(i=0;i<expr->degree;i++) {
					res = tupleComps[i] = evaluateExpression3(expr->subtrees[i], applyAll, 0, rei, reiSaveFlag, env, errmsg, r);
					if(res->nodeType == N_ERROR) {
						break;
					}
				}
				if(expr->degree == 0 || res->nodeType != N_ERROR) {
					if(expr->value.constructTuple || expr->degree != 1) {
						res = newTupleRes(expr->degree, tupleComps, r);
					}
				}
				break;
			case N_ACTIONS_RECOVERY:
							res = evaluateActions(expr->subtrees[0], expr->subtrees[1], rei, reiSaveFlag, env, errmsg, r);
							break;

			case N_ACTIONS:
							generateErrMsg("error: evaluate actions using function evaluateExpression3, use function evaluateActions instead.", expr->expr, expr->base, errbuf);
							addRErrorMsg(errmsg, -1, errbuf);
							res = newErrorRes(r, -1);
							break;
					default:
							generateErrMsg("error: unsupported ast node type.", expr->expr, expr->base, errbuf);
							addRErrorMsg(errmsg, -1, errbuf);
							res = newErrorRes(r, -1);
							break;
		}
    } else {
    	res = expr;
    	while(res->nodeType == N_TUPLE && res->degree == 1) {
    		res = res->subtrees[0];
    	}
    }
        /* coercions are applied at application locations only */
        return res;
}

Res* processCoercion(Node *node, Res *res, ExprType *type, Hashtable *tvarEnv, rError_t *errmsg, Region *r) {
        char buf[ERR_MSG_LEN>1024?ERR_MSG_LEN:1024];
        char *buf2;
        char buf3[ERR_MSG_LEN];
        ExprType *coercion = type;
        if(coercion->nodeType == T_FLEX) {
            coercion = coercion->subtrees[0];
        }
        coercion = instantiate(coercion, tvarEnv, 0, r);
        if(coercion->nodeType == T_VAR) {
            if(T_VAR_NUM_DISJUNCTS(coercion) == 0) {
                /* generateErrMsg("error: cannot instantiate coercion type for node.", node->expr, node->base, buf);
                addRErrorMsg(errmsg, -1, buf);
                return newErrorRes(r, -1); */
                return res;
            }
            /* here T_VAR must be a set of bounds
             * we fix the set of bounds to the default bound */
            ExprType *defaultType = T_VAR_DISJUNCT(coercion,0);
            updateInHashTable(tvarEnv, getTVarName(T_VAR_ID(coercion), buf), defaultType);
            coercion = defaultType;
        }
        if(typeEqSyntatic(coercion, res->exprType)) {
            return res;
        } else {
            switch(coercion->nodeType) {
                case T_DYNAMIC:
                    return res;
                case T_INT:
                    switch(TYPE(res) ) {
                        case T_DOUBLE:
                        case T_BOOL:
                            if((int)res->value.dval!=res->value.dval) {
                                generateErrMsg("error: dynamic type conversion  double -> int: the double is not an integer", node->expr, node->base, buf);
                                addRErrorMsg(errmsg, -1, buf);
                                return newErrorRes(r, -1);
                            } else {
                                return newIntRes(r, (int)res->value.dval);
                            }
                        case T_STRING:
                            return newIntRes(r, atoi(res->text));
                        default:
                            break;
                    }
                    break;
                case T_DOUBLE:
                    switch(TYPE(res) ) {
                        case T_INT:
                        case T_BOOL:
                            return newDoubleRes(r, res->value.dval);
                        case T_STRING:
                            return newDoubleRes(r, atof(res->text));
                        default:
                            break;
                    }
                    break;
                case T_STRING:
                    switch(TYPE(res) ) {
                        case T_INT:
                        case T_DOUBLE:
                        case T_BOOL:
                            buf2 = convertResToString(res);

                            res = newStringRes(r, buf2);
                            free(buf2);
                            return res;
                        default:
                            break;
                    }
                    break;
                case T_BOOL:
                    switch(TYPE(res) ) {
                        case T_INT:
                        case T_DOUBLE:
                            return newBoolRes(r, res->value.dval);
                        case T_STRING:
                            if(strcmp(res->text, "true")==0) {
                                return newBoolRes(r, 1);
                            } else if(strcmp(res->text, "false")==0) {
                                return newBoolRes(r, 0);
                            } else {
                                generateErrMsg("error: dynamic type conversion  string -> bool: the string is not in {true, false}", node->expr, node->base, buf);
                                addRErrorMsg(errmsg, -1, buf);
                                return newErrorRes(r, -1);
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                case T_CONS:
                    /* we can ignore the not top level type constructor and leave type checking to when it is derefed */
                    switch(TYPE(res)) {
                        case T_CONS:
                            return res;
                        case T_IRODS:
                            if(strcmp(res->exprType->text, IntArray_MS_T) == 0 ||
                               strcmp(res->exprType->text, StrArray_MS_T) == 0 ||
                               strcmp(res->exprType->text, GenQueryOut_MS_T) == 0) {
                                return res;
                            }
                        default:
                            break;
                    }
                case T_DATETIME:
                    switch(TYPE(res)) {
                        case T_INT:
                            newDatetimeRes(r, (time_t) res->value.dval);
                        default:
                            break;
                    }
                default:
                    break;
            }
            char typeBuf1[128], typeBuf2[128];
            snprintf(buf, ERR_MSG_LEN, "error: coerce from type %s to type %s",
            		typeToString(res->exprType, tvarEnv, typeBuf1, 128),
            		typeToString(coercion, tvarEnv, typeBuf2, 128));
            generateErrMsg(buf, node->expr, node->base, buf3);
            addRErrorMsg(errmsg, -1, buf3);
            res->nodeType=N_ERROR;
            res->value.errcode = -1;
            return res;
        }
}

Res* evaluateActions(Node *expr, Node *reco, ruleExecInfo_t *rei, int reiSaveFlag, Env *env, rError_t* errmsg, Region *r) {
/*
    printTree(expr, 0);
*/
    int i;
    int cutFlag = 0;
    Res* res = NULL;
    #ifndef DEBUG
    char tmpStr[1024];
    #endif
	switch(expr->nodeType) {
		case N_ACTIONS:
            for(i=0;i<expr->degree;i++) {
                Node *nodei = expr->subtrees[i];
                if(nodei->nodeType == N_APPLICATION && nodei->subtrees[0]->nodeType == TK_TEXT && strcmp(nodei->subtrees[0]->text, "cut") == 0) {
                    cutFlag = 1;
                    continue;
                }
                res = evaluateExpression3(nodei, 0, 0, rei, reiSaveFlag, env, errmsg,r);
                if(res->nodeType == N_ERROR) {
                    #ifndef DEBUG
                        sprintf(tmpStr,"executeRuleAction Failed for %s",nodei->text);
                        rodsLogError(LOG_ERROR,res->value.errcode,tmpStr);
                        rodsLog (LOG_NOTICE,"executeRuleBody: Micro-service or Action %s Failed with status %i",nodei->text,res->value.errcode);
                    #endif
                    /* run recovery chain */
                    if(res->value.errcode != RETRY_WITHOUT_RECOVERY_ERR && reco!=NULL) {
                        int i2;
                        for(i2 = reco->degree-1>i?reco->degree-1:i;i2>=0;i2--) {
                            #ifndef DEBUG
                                if (reTestFlag > 0) {
                                        if (reTestFlag == COMMAND_TEST_1 || COMMAND_TEST_MSI)
                                                fprintf(stdout,"***RollingBack\n");
                                        else if (reTestFlag == HTML_TEST_1)
                                                fprintf(stdout,"<FONT COLOR=#FF0000>***RollingBack</FONT><BR>\n");
                                        else  if (reTestFlag == LOG_TEST_1)
                                                if (rei != NULL && rei->rsComm != NULL && &(rei->rsComm->rError) != NULL)
                                                        rodsLog (LOG_NOTICE,"***RollingBack\n");
                                }
                            #endif

                            Res *res2 = evaluateExpression3(reco->subtrees[i2], 0, 0, rei, reiSaveFlag, env, errmsg, r);
                            if(res2->nodeType == N_ERROR) {
                            #ifndef DEBUG
                                sprintf(tmpStr,"executeRuleRecovery Failed for %s",reco->subtrees[i2]->text);
                                rodsLogError(LOG_ERROR,res2->value.errcode,tmpStr);
                            #endif
                            }
                        }
                    }
                    if(cutFlag) {
                        return newErrorRes(r, CUT_ACTION_PROCESSED_ERR);
                    } else {
                        return res;
                    }
                }
                else if(TYPE(res) == T_BREAK) {
                    return res;
                }
                else if(TYPE(res) == T_SUCCESS) {
                    return res;
                }

            }
            return res==NULL?newIntRes(r, 0):res;
        default:
            break;
	}
    char errbuf[ERR_MSG_LEN];
    generateErrMsg("error: unsupported ast node type.", expr->expr, expr->base, errbuf);
	addRErrorMsg(errmsg, -1, errbuf);
	return newErrorRes(r, -1);
}

Res *evaluateFunctionApplication(Node *func, Node *arg, int applyAll, Node *node, ruleExecInfo_t* rei, int reiSaveFlag, Env *env, rError_t *errmsg, Region *r) {
    Res *res;
    char errbuf[ERR_MSG_LEN];
    switch(func->nodeType) {
        case N_FUNC_SYM_LINK:
        case N_PARTIAL_APPLICATION:
            res = newPartialApplication(func, arg, func->value.nArgs - 1, r);
            if(res->value.nArgs == 0) {
                res = evaluateFunction3(res, applyAll, node, env, rei, reiSaveFlag, errmsg, r);
            }
            return res;
        default:
            generateErrMsg("unsupported function node type.", node->expr, node->base, errbuf);
            addRErrorMsg(errmsg, -1, errbuf);
            return newErrorRes(r, -1);
    }
}

void printEnvIndent(Env *env) {
	Env *e = env->lower;
	int i =0;
		while(e!=NULL) {
			i++;
			e=e->lower;
		}
		printIndent(i);
}
/**
 * evaluate function
 * provide env and region isolation for rules and external microservices
 * precond n <= MAX_PARAMS_LEN
 */
Res* evaluateFunction3(Node *appRes, int applyAll, Node *node, Env *env, ruleExecInfo_t* rei, int reiSaveFlag, rError_t *errmsg, Region *r) {
    unsigned int i;
	unsigned int n;
	Node* subtrees0[MAX_FUNC_PARAMS];
	Node* args[MAX_FUNC_PARAMS];
	i = 0;
	Node *appFuncRes = appRes;
    while(appFuncRes->nodeType == N_PARTIAL_APPLICATION) {
        i++;
        subtrees0[MAX_FUNC_PARAMS - i] = appFuncRes->subtrees[1];
        appFuncRes = appFuncRes->subtrees[0];
    }
    /* app can only be N_FUNC_SYM_LINK */
    char* fn = appFuncRes->text;
	if(strcmp(fn, "nop") == 0) {
    	return newIntRes(r, 0);
    }
/*    printEnvIndent(env);
	printf("calling function %s\n", fn);
	char buf0[ERR_MSG_LEN];
	generateErrMsg("", node->expr, node->base, buf0);
	printf("%s", buf0); */
    Res *appArgRes = appRes->subtrees[1];

    n = appArgRes->degree;
    Res** appArgs = appArgRes->subtrees;
    Node** nodeArgs = node->subtrees[1]->subtrees;
    ExprType *coercionType = NULL;
    #ifdef DEBUG
    char buf[ERR_MSG_LEN>1024?ERR_MSG_LEN:1024];
	sprintf(buf, "Action: %s\n", fn);
    writeToTmp("eval.log", buf);
    #endif
    /* char buf2[ERR_MSG_LEN]; */

    Res* res;
    Region *newRegion = make_region(0, NULL);
    Env *global = globalEnv(env);
    Env *nEnv = newEnv(newHashTable(100), global, env);

    List *localTypingConstraints = NULL;
    FunctionDesc *fd = NULL;
    /* look up function descriptor */
    fd = (FunctionDesc *)lookupFromHashTable(ruleEngineConfig.funcDescIndex, fn);
        /* find matching arity */
/*    if(fd!=NULL) {
        if((fd->exprType->vararg == ONCE && T_FUNC_ARITY(fd->exprType) == n) ||
            (fd->exprType->vararg == STAR && T_FUNC_ARITY(fd->exprType) - 2 <= n) ||
            (fd->exprType->vararg == PLUS && T_FUNC_ARITY(fd->exprType) - 1 <= n)) {
        } else {
            snprintf(buf, ERR_MSG_LEN, "error: arity mismatch for function %s", fn);
            generateErrMsg(buf, node->expr, node->base, buf2);
            addRErrorMsg(errmsg, -1, buf2);
            res = newErrorRes(r, -1);
            RETURN;
        }
    }*/

    localTypingConstraints = newList(r);
    char ioParam[MAX_FUNC_PARAMS];
    /* evaluation parameters and try to resolve remaining tvars with unification */
    for(i=0;i<n;i++) {
        switch(appArgs[i]->iotype) {
            case IO_TYPE_INPUT | IO_TYPE_OUTPUT: /* input/output */
                ioParam[i] = 'p';
                args[i] = evaluateExpression3(appArgs[i], applyAll, 0, rei, reiSaveFlag, env, errmsg, newRegion);
                if(args[i]->nodeType==N_ERROR) {
                    res = (Res *)args[i];
                    RETURN;
                }
                break;
            case IO_TYPE_INPUT: /* input */
                ioParam[i] = 'i';
                args[i] = appArgs[i];
                break;
            case IO_TYPE_DYNAMIC: /* dynamic */
            	if(isVariableNode(appArgs[i])) {
					args[i] = attemptToEvaluateVar3(appArgs[i]->text, appArgs[i], rei, reiSaveFlag, env, errmsg, newRegion);
					if(args[i] == NULL) { // output only
						ioParam[i] = 'o';
						args[i] = newUnspecifiedRes(r);
					} else {
						ioParam[i] = 'p';
						if(args[i]->nodeType==N_ERROR) {
							res = (Res *)args[i];
							RETURN;
						}
					}
            	} else {
            		ioParam[i] = 'i';
            		args[i] = evaluateExpression3(appArgs[i], applyAll, 1, rei, reiSaveFlag, env, errmsg, newRegion);
					if(args[i]->nodeType==N_ERROR) {
						res = (Res *)args[i];
						RETURN;
					}
            	}
                break;
            case IO_TYPE_OUTPUT: /* output */
                ioParam[i] = 'o';
                args[i] = newStringRes(r, "");
                break;
            case IO_TYPE_EXPRESSION: /* expression */
                ioParam[i] = 'e';
                args[i] = appArgs[i];
                break;
            case IO_TYPE_ACTIONS: /* actions */
                ioParam[i] = 'a';
                args[i] = appArgs[i];
                break;
        }
    }
    /* try to type all input parameters */
    coercionType = node->subtrees[1]->coercionType;
    if(coercionType!=NULL) {
    	/*for(i=0;i<n;i++) {
			 if((ioParam[i] == 'i' || ioParam[i] == 'p') && nodeArgs[i]->exprType != NULL) {
				if(unifyWith(args[i]->exprType, nodeArgs[i]->exprType, env->current, r) == NULL) {
					snprintf(buf, ERR_MSG_LEN, "error: dynamically typed parameter does not have expected type.");
					                    generateErrMsg(buf, nodeArgs[i]->expr, nodeArgs[i]->base, buf2);
					                    addRErrorMsg(errmsg, TYPE_ERROR, buf2);
					                    res = newErrorRes(r, TYPE_ERROR);
					                    RETURN;
				}
			}
		}*/


        ExprType *argType = newTupleRes(n, args, r)->exprType;
		if(typeFuncParam(node->subtrees[1], argType, coercionType, env->current, localTypingConstraints, errmsg, newRegion)!=0) {
			res = newErrorRes(r, TYPE_ERROR);
			RETURN;
		}
		/* solve local typing constraints */
		/* typingConstraintsToString(localTypingConstraints, localTVarEnv, buf, 1024);
		printf("%s\n", buf); */
		Node *errnode;
		if(!solveConstraints(localTypingConstraints, env->current, errmsg, &errnode, r)) {
			res = newErrorRes(r, -1);
			RETURN;
		}
		/*printVarTypeEnvToStdOut(localTVarEnv); */
		/* do the input value conversion */
		ExprType **coercionTypes = coercionType->subtrees;
		for(i=0;i<n;i++) {
			if((ioParam[i] == 'i' || ioParam[i] == 'p') && nodeArgs[i]->coerce) {
				args[i] = processCoercion(nodeArgs[i], args[i], coercionTypes[i], env->current, errmsg, newRegion);
				if(args[i]->nodeType==N_ERROR) {
					res = (Res *)args[i];
					RETURN;
				}
			}
		}
    }


    if(fd!=NULL) {
        switch(fd->nodeType) {
            case N_DECONSTRUCTOR:
                res = deconstruct(fn, args, n, fd->value.proj, errmsg, r);
                break;
            case N_CONSTRUCTOR:
                res = construct(fn, args, n, instantiate(node->exprType, env->current, 1, r), r);
                break;
            case N_C_FUNC:
                res = (Res *) fd->value.func(args, n, node, rei, reiSaveFlag,  env, errmsg, newRegion);
                break;
            default:
                printf("error!");
                RETURN;
        }
    } else {
        res = execAction3(fn, args, n, applyAll, node, nEnv, rei, reiSaveFlag, errmsg, newRegion);
    }

    if(res->nodeType==N_ERROR) {
        RETURN;
    }

    for(i=0;i<n;i++) {
        Res *resp = NULL;

        if(ioParam[i] == 'o' || ioParam[i] == 'p') {
            if(appArgs[i]->coerce) {
                args[i] = processCoercion(nodeArgs[i], args[i], appArgs[i]->exprType, env->current, errmsg, newRegion);
            }
            if(args[i]->nodeType==N_ERROR) {
                res = (Res *)args[i];
                RETURN ;
            }
            resp = setVariableValue(appArgs[i]->text, args[i],rei,env,errmsg,r);
            /*char *buf = convertResToString(args[i]);
            printEnvIndent(env);
            printf("setting variable %s to %s\n", appArgs[i]->text, buf);
            free(buf);*/
        }
        if(resp!=NULL && resp->nodeType==N_ERROR) {
            res = resp;
            RETURN;
        }
    }
    /*printEnvIndent(env);
	printf("exiting function %s\n", fn);
*/
    /*return res;*/
ret:
    deleteEnv(nEnv, 2);
    cpEnv(env,r);
    res = cpRes(res,r);
    region_free(newRegion);
    return res;

}

Res* attemptToEvaluateVar3(char* vn, Node *node, ruleExecInfo_t *rei, int reiSaveFlag, Env *env, rError_t *errmsg, Region *r) {
	if(vn[0]=='*') { /* local variable */
		/* try to get local var from env */
		Res* res0 = (Res *)lookupFromEnv(env, vn);
        return res0;
	} else if(vn[0]=='$') { /* session variable */
        Res *res = getSessionVar("",vn,rei, env, errmsg,r);
        return res;
	} else {
        return NULL;
	}
}

Res* evaluateVar3(char* vn, Node *node, ruleExecInfo_t *rei, int reiSaveFlag, Env *env, rError_t *errmsg, Region *r) {
    char buf[ERR_MSG_LEN>1024?ERR_MSG_LEN:1024];
    char buf2[ERR_MSG_LEN];
    Res *res = attemptToEvaluateVar3(vn, node, rei, reiSaveFlag, env, errmsg, r);
    if(res == NULL) {
        if(vn[0]=='*') { /* local variable */
		    snprintf(buf, ERR_MSG_LEN, "error: unable to read local variable %s.", vn);
                    generateErrMsg(buf, node->expr, node->base, buf2);
                    addRErrorMsg(errmsg, UNABLE_TO_READ_LOCAL_VAR, buf2);
                    /*printEnvToStdOut(env); */
                    return newErrorRes(r, UNABLE_TO_READ_LOCAL_VAR);
        } else if(vn[0]=='$') { /* session variable */
    	    snprintf(buf, ERR_MSG_LEN, "error: unable to read session variable %s.", vn);
            generateErrMsg(buf, node->expr, node->base, buf2);
            addRErrorMsg(errmsg, UNABLE_TO_READ_SESSION_VAR, buf2);
            return newErrorRes(r, UNABLE_TO_READ_SESSION_VAR);
        } else {
            snprintf(buf, ERR_MSG_LEN, "error: unable to read variable %s.", vn);
            generateErrMsg(buf, node->expr, node->base, buf2);
            addRErrorMsg(errmsg, UNABLE_TO_READ_VAR, buf2);
            return newErrorRes(r, UNABLE_TO_READ_VAR);
        }
	}
	return res;
}

/**
 * return NULL error
 *        otherwise success
 */
Res* getSessionVar(char *action,  char *varName,  ruleExecInfo_t *rei, Env *env, rError_t *errmsg, Region *r) {
  char *varMap;
  void *varValue = NULL;
  int i, vinx;
  varValue = NULL;

  /* Maps varName to the standard name and make varMap point to it. */
  /* It seems that for each pair of varName and standard name there is a list of actions that are supported. */
  /* vinx stores the index of the current pair so that we can start for the next pair if the current pair fails. */
  vinx = getVarMap(action,varName, &varMap, 0); /* reVariableMap.c */
  while (vinx >= 0) {
	/* Get the value of session variable referenced by varMap. */
      i = getVarValue(varMap, rei, (char **)&varValue); /* reVariableMap.c */
      /* convert to char * because getVarValue requires char * */
      if (i >= 0) {
            if (varValue != NULL) {
                Res *res = NULL;
                FunctionDesc *fd = (FunctionDesc *) lookupBucketFromHashTable(ruleEngineConfig.funcDescIndex, varMap);
                if (fd == NULL) {
                    /* default to string */
                    res = newStringRes(r, (char *) varValue);
                    free(varValue);
                } else {
                    ExprType *type = T_FUNC_RET_TYPE(fd->exprType); /* get var type from varMap */
                    switch (type->nodeType) {
                        case T_STRING:
                            res = newStringRes(r, (char *) varValue);
                            free(varValue);
                            break;
                        case T_INT:
                            res = newIntRes(r, *(int *) varValue);
                            free(varValue);
                            break;
                        case T_DOUBLE:
                            res = newDoubleRes(r, *(double *) varValue);
                            free(varValue);
                            break;
                        case T_IRODS:
                            res = newRes(r);
                            res->value.uninterpreted.inOutStruct = varValue;
                            res->value.uninterpreted.inOutBuffer = NULL;
                            res->exprType = type;
                            break;
                        default:
                            /* unsupported type error */
                            res = NULL;
                            addRErrorMsg(errmsg, -1, "error: unsupported session variable type");
                    }
                }
                free(varMap);
                return res;
            } else {
                return NULL;
            }
    } else if (i == NULL_VALUE_ERR) { /* Try next varMap, starting from vinx+1. */
      free(varMap);
      vinx = getVarMap(action,varName, &varMap, vinx+1);
    } else { /* On error, return 0. */
      free(varMap);
      if (varValue != NULL) free (varValue);
      return NULL;
    }
  }
  /* varMap not found, return 0. */
  if (varValue != NULL) free (varValue);
  return NULL;
}

/*
 * execute an external microserive or a rule
 */
Res* execAction3(char *actionName, Res** args, unsigned int nargs, int applyAllRule, Node *node, Env *env, ruleExecInfo_t* rei, int reiSaveFlag, rError_t *errmsg, Region *r)
{
	char buf[ERR_MSG_LEN>1024?ERR_MSG_LEN:1024];
	char buf2[ERR_MSG_LEN];
	char action[MAX_COND_LEN];
	strcpy(action, actionName);
	mapExternalFuncToInternalProc2(action);

	int actionInx = actionTableLookUp(action);
	if (actionInx < 0) { /* rule */
		/* no action (microservice) found, try to lookup a rule */
		Res *actionRet = execRule(actionName, args, nargs, applyAllRule, env, rei, reiSaveFlag, errmsg, r);
		if (actionRet->nodeType == N_ERROR && (
                        actionRet->value.errcode == NO_RULE_FOUND_ERR ||
                        actionRet->value.errcode == NO_MORE_RULES_ERR)) {
			snprintf(buf, ERR_MSG_LEN, "error: cannot find rule for action \"%s\" available: %d.", action, availableRules());
                        generateErrMsg(buf, node->expr, node->base, buf2);
                        addRErrorMsg(errmsg, NO_RULE_OR_MSI_FUNCTION_FOUND_ERR, buf2);
                        /*dumpHashtableKeys(coreRules); */
			return newErrorRes(r, NO_RULE_OR_MSI_FUNCTION_FOUND_ERR);
		}
		else {
			return actionRet;
		}
	} else { /* microservice */
		return execMicroService3(action, args, nargs, node, env, rei, errmsg, r);
	}
}

/**
 * execute micro service msiName
 */
Res* execMicroService3 (char *msName, Res **args, unsigned int nargs, Node *node, Env *env, ruleExecInfo_t *rei, rError_t *errmsg, Region *r) {
        msParamArray_t *origMsParamArray = rei->msParamArray;
	funcPtr myFunc = NULL;
	int actionInx;
	unsigned int numOfStrArgs;
	unsigned int i;
	int ii;
	msParam_t *myArgv[MAX_PARAMS_LEN];
    Res *res;

	/* look up the micro service */
	actionInx = actionTableLookUp(msName);

        char errbuf[ERR_MSG_LEN];
	if (actionInx < 0) {
            int ret = NO_MICROSERVICE_FOUND_ERR;
            generateErrMsg("execMicroService3: no micro service found", node->expr, node->base, errbuf);
            addRErrorMsg(errmsg, ret, errbuf);
            return newErrorRes(r, ret);

        }
	myFunc =  MicrosTable[actionInx].callAction;
	numOfStrArgs = MicrosTable[actionInx].numberOfStringArgs;
	if (nargs != numOfStrArgs) {
            int ret = ACTION_ARG_COUNT_MISMATCH;
            generateErrMsg("execMicroService3: wrong number of arguments", node->expr, node->base, errbuf);
            addRErrorMsg(errmsg, ret, errbuf);
            return newErrorRes(r, ret);
        }

	/* convert arguments from Res to msParam_t */
    char buf[1024];
	for (i = 0; i < numOfStrArgs; i++) {
            myArgv[i] = (msParam_t *)malloc(sizeof(msParam_t));
            Res *res = args[i];
            if(res != NULL) {
                int ret =
                    convertResToMsParam(myArgv[i], res, errmsg);
                if(ret!=0) {
                    generateErrMsg("execMicroService3: error converting arguments to MsParam", node->subtrees[i]->expr, node->subtrees[i]->base, errbuf);
                    addRErrorMsg(errmsg, ret, errbuf);
                    int j = i;
                    for(;j>=0;j--) {
                        if(TYPE(args[j])!=T_IRODS) {
                            free(myArgv[j]->inOutStruct);
                        }
                        free(myArgv[j]->label);
                        free(myArgv[j]->type);
                        free(myArgv[j]);
                    }
                    return newErrorRes(r, ret);
                }
            } else {
                myArgv[i]->inOutStruct = NULL;
                myArgv[i]->inpOutBuf = NULL;
                myArgv[i]->type = strdup(STR_MS_T);
            }
            sprintf(buf,"**%d",i);
            myArgv[i]->label = strdup(buf);
	}
        /* convert env to msparam array */
        rei->msParamArray = newMsParamArray();
        int ret = convertEnvToMsParamArray(rei->msParamArray, env, errmsg, r);
        if(ret!=0) {
            generateErrMsg("execMicroService3: error converting env to MsParamArray", node->expr, node->base, errbuf);
            addRErrorMsg(errmsg, ret, errbuf);
            res = newErrorRes(r,ret);
            RETURN;
        }


	if (numOfStrArgs == 0)
		ii = (*(int (*)(ruleExecInfo_t *))myFunc) (rei) ;
	else if (numOfStrArgs == 1)
		ii = (*(int (*)(msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],rei);
	else if (numOfStrArgs == 2)
		ii = (*(int (*)(msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],rei);
	else if (numOfStrArgs == 3)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],rei);
	else if (numOfStrArgs == 4)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],myArgv[3],rei);
	else if (numOfStrArgs == 5)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],myArgv[3],myArgv[4],rei);
	else if (numOfStrArgs == 6)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],myArgv[3],myArgv[4],myArgv[5],rei);
	else if (numOfStrArgs == 7)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],myArgv[3],myArgv[4],myArgv[5],myArgv[6],rei);
	else if (numOfStrArgs == 8)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],myArgv[3],myArgv[4],myArgv[5],myArgv[6],myArgv[7],rei);
	else if (numOfStrArgs == 9)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],myArgv[3],myArgv[4],myArgv[5],myArgv[6],myArgv[7],myArgv[8],rei);
	else if (numOfStrArgs == 10)
		ii = (*(int (*)(msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, msParam_t *, ruleExecInfo_t *))myFunc) (myArgv[0],myArgv[1],myArgv[2],myArgv[3],myArgv[4],myArgv[5],myArgv[6],myArgv[7],
		                myArgv[8],myArgv [9],rei);
    if(ii<0) {
        res = newErrorRes(r, ii);
        RETURN;
    }
    /* converts back env */
	ret = updateMsParamArrayToEnv(rei->msParamArray, env, errmsg, r);
    if(ret!=0) {
        generateErrMsg("execMicroService3: error env from MsParamArray", node->expr, node->base, errbuf);
        addRErrorMsg(errmsg, ret, errbuf);
        res = newErrorRes(r, ret);
        RETURN;
    }
    /* params */
	for (i = 0; i < numOfStrArgs; i++) {
        if(myArgv[i] != NULL) {
            int ret =
                convertMsParamToRes(myArgv[i], args[i], errmsg, r);
            if(ret!=0) {
                generateErrMsg("execMicroService3: error converting arguments from MsParam", node->expr, node->base, errbuf);
                addRErrorMsg(errmsg, ret, errbuf);
                res= newErrorRes(r, ret);
                RETURN;
            }
        } else {
            args[i] = NULL;
        }
	}

/*
        char vn[100];
	for(i=0;i<numOfStrArgs;i++) {
            sprintf(vn,"**%d",i);
            largs[i] = lookupFromHashTable(env, vn);
	}
*/
	res = newIntRes(r, ii);
ret:
    if(rei->msParamArray!=NULL && rei->msParamArray != origMsParamArray) {
        deleteMsParamArray(rei->msParamArray);
        rei->msParamArray = origMsParamArray;
    }
    for(i=0;i<numOfStrArgs;i++) {
        if(TYPE(args[i])!=T_IRODS) {
            free(myArgv[i]->inOutStruct);
        }
        free(myArgv[i]->label);
        free(myArgv[i]->type);
        free(myArgv[i]);
    }
    if(res->nodeType==N_ERROR) {
        generateErrMsg("execMicroService3: error when executing microservice", node->expr, node->base, errbuf);
        addRErrorMsg(errmsg, res->value.errcode, errbuf);
    }
    return res;
}

Res* execRuleFromCondIndex(char *ruleName, Res **args, int argc, CondIndexVal *civ, Env *env, ruleExecInfo_t *rei, int reiSaveFlag, rError_t *errmsg, Region *r) {
            /*printTree(civ->condExp, 0); */
        Res *status;
        Env *envNew = newEnv(newHashTable(100), globalEnv(env), env);
        Node* rule = NULL;
        RuleIndexListNode *indexNode = NULL;
        Res* res = NULL;


        if(civ->params->degree != argc) {
            char buf[ERR_MSG_LEN];
            snprintf(buf, ERR_MSG_LEN, "error: cannot apply rule %s from rule conditional index because of wrong number of arguments, declared %d, supplied %d.", ruleName, civ->params->degree, argc);
            addRErrorMsg(errmsg, ACTION_ARG_COUNT_MISMATCH, buf);
            return newErrorRes(r, ACTION_ARG_COUNT_MISMATCH);
        }

        int i = initializeEnv(civ->params,args,argc, envNew->current,r);
        if (i != 0) {
            status = newErrorRes(r, i);
            RETURN;
        }

        res = evaluateExpression3(civ->condExp, 0, 0, rei, reiSaveFlag, envNew,  errmsg, r);
        if(res->nodeType == N_ERROR) {
            status = res;
            RETURN;
        }
        if(TYPE(res) != T_STRING) {
            /* todo try coercion */
            addRErrorMsg(errmsg, -1, "error: the lhs of indexed rule condition does not evaluate to a string");
            status = newErrorRes(r, -1);
            RETURN;
        }

        indexNode = (RuleIndexListNode *)lookupFromHashTable(civ->valIndex, res->text);
        if(indexNode == NULL) {
#ifndef DEBUG
            rodsLog (LOG_NOTICE,"applyRule Failed for action 1: %s with status %i",ruleName, NO_MORE_RULES_ERR);
#endif
            status = newErrorRes(r, NO_MORE_RULES_ERR);
            RETURN;
        }

        rule = getRuleNode(indexNode->ruleIndex+CORE_RULE_INDEX_OFF); /* increase the index to move it to core rules index below MAX_NUM_APP_RULES are app rules */

        status = execRuleNodeRes(rule, args, argc,  env, rei, reiSaveFlag, errmsg, r);

        if (status->nodeType == N_ERROR) {
            rodsLog (LOG_NOTICE,"applyRule Failed for action : %s with status %i",ruleName, status->value.errcode);
        }

    ret:
        deleteEnv(envNew, 2);
        if(TYPE(status) == T_SUCCESS) {
            status = newIntRes(r, 0);
        }

        return status;


}
/*
 * look up rule node by rulename from index
 * apply rule condition index if possilbe
 * call execRuleNodeRes until success or no more rules
 */
Res *execRule(char *ruleNameInp, Res** args, unsigned int argc, int applyAllRuleInp, Env *env, ruleExecInfo_t *rei, int reiSaveFlag, rError_t *errmsg, Region *r)
{
    int ruleInx, statusI;
    Res *statusRes;
    int  inited = 0;
    ruleExecInfo_t  *saveRei;
    int reTryWithoutRecovery = 0;
    char ruleName[MAX_COND_LEN];
    int applyAllRule = applyAllRuleInp | GlobalAllRuleExecFlag;

    #ifdef DEBUG
    char buf[1024];
    snprintf(buf, 1024, "execRule: %s\n", ruleNameInp);
    writeToTmp("entry.log", buf);
    #endif

    ruleInx = -1; /* new rule */
    strcpy(ruleName, ruleNameInp);
    mapExternalFuncToInternalProc2(ruleName);

    /* try to find rule by cond index */
    if(ruleEngineConfig.condIndexStatus == INITIALIZED) { /* if index has been initialized */
        CondIndexVal *civ = (CondIndexVal *)lookupFromHashTable(ruleEngineConfig.condIndex, ruleName);
        if(civ != NULL) {
            statusRes = execRuleFromCondIndex(ruleName, args, argc, civ,  env, rei, reiSaveFlag, errmsg, r);
            /* restore global flag */
            return statusRes;
        }
    }


    int success = 0;
    int first = 1;
    while (1) {
        statusI = findNextRule2(ruleName, &ruleInx);

        if (statusI != 0) {
            if (statusI == NO_MORE_RULES_ERR) {
                if(applyAllRule == 0) {
        #ifndef DEBUG
                    rodsLog (LOG_NOTICE,"applyRule Failed for action 1: %s with status %i",ruleName, statusI);
        #endif
                    statusRes = newErrorRes(r, NO_RULE_FOUND_ERR);
                } else { // apply all rules succeeds even when 0 rule is applied
                    success = 1;
                }
            }
            break;
        }

        Node* rule = getRuleNode(ruleInx);
        unsigned int inParamsCount = RULE_NODE_NUM_PARAMS(rule);
        if (inParamsCount != argc) {
            continue;
        }

        if(!first) {
            addRErrorMsg(errmsg, statusI, ERR_MSG_SEP);
        } else {
            first = 0;
        }

        if (GlobalREDebugFlag)
            reDebug("  GotRule", ruleInx, ruleName, NULL, rei); /* pass in NULL for inMsParamArray for now */
#ifndef DEBUG
        if (reTestFlag > 0) {
            if (reTestFlag == COMMAND_TEST_1)
                fprintf(stdout, "+Testing Rule Number:%i for Action:%s\n", ruleInx, ruleName);
            else if (reTestFlag == HTML_TEST_1)
                fprintf(stdout, "+Testing Rule Number:<FONT COLOR=#FF0000>%i</FONT> for Action:<FONT COLOR=#0000FF>%s</FONT><BR>\n", ruleInx, ruleName);
            else if (rei != 0 && rei->rsComm != 0 && &(rei->rsComm->rError) != 0)
                rodsLog(LOG_NOTICE, "+Testing Rule Number:%i for Action:%s\n", ruleInx, ruleName);
        }
#endif
        /* printTree(rule, 0); */
        if (reiSaveFlag == SAVE_REI) {
            int statusCopy = 0;
            if (inited == 0) {
                saveRei = (ruleExecInfo_t *) mallocAndZero(sizeof (ruleExecInfo_t));
                statusCopy = copyRuleExecInfo(rei, saveRei);
                inited = 1;
            } else if (reTryWithoutRecovery == 0) {
                statusCopy = copyRuleExecInfo(saveRei, rei);
            }
            if(statusCopy != 0) {
                statusRes = newErrorRes(r, statusCopy);
                break;
            }
        }

        statusRes = execRuleNodeRes(rule, args, argc,  env, rei, reiSaveFlag, errmsg, r);

        int ret = 0;
        if(statusRes->nodeType!=N_ERROR) {
            success = 1;
            if (applyAllRule == 0) { /* apply first rule */
                ret = 1;
            } else { /* apply all rules */
                if (reiSaveFlag == SAVE_REI) {
                    freeRuleExecInfoStruct(saveRei, 0);
                    inited = 0;
                }
            }
        } else if(statusRes->value.errcode== RETRY_WITHOUT_RECOVERY_ERR) {
            reTryWithoutRecovery = 1;
        } else if(statusRes->value.errcode== CUT_ACTION_PROCESSED_ERR) {
            ret = 1;
        }

        if (ret) {
            break;
        }

    }

    if (inited == 1) {
        freeRuleExecInfoStruct(saveRei, 0);
    }

    if(success) {
        if(applyAllRule) {
            /* if we apply all rules, then it succeeds even if some of the rules fail */
            return newIntRes(r, 0);
        } else if(TYPE(statusRes) == T_SUCCESS) {
            return newIntRes(r, 0);
        } else {
            return statusRes;
        }
    } else {
#ifndef DEBUG
            rodsLog (LOG_NOTICE,"applyRule Failed for action 2: %s with status %i",ruleName, statusRes->value.errcode);
#endif
        return statusRes;
    }
}
void copyFromEnv(Res **args, char **inParams, int inParamsCount, Hashtable *env, Region *r) {
	int i;
	for(i=0;i<inParamsCount;i++) {
		args[i]= cpRes((Res *)lookupFromHashTable(env, inParams[i]),r);
	}
}
/*
 * execute a rule given by an AST node
 * create a new environment and intialize it with parameters
 */
Res* execRuleNodeRes(Node *rule, Res** args, unsigned int argc, Env *env, ruleExecInfo_t *rei, int reiSaveFlag, rError_t *errmsg, Region *r)
{
	Node* ruleCondition = rule->subtrees[1];
	Node* ruleAction = rule->subtrees[2];
	Node* ruleRecovery = rule->subtrees[3];
	Node* ruleHead = rule->subtrees[0];
    Node** paramsNodes = ruleHead->subtrees[0]->subtrees;
	char* paramsNames[MAX_NUM_OF_ARGS_IN_ACTION];
        int inParamsCount = RULE_NODE_NUM_PARAMS(rule);
        Res *statusRes;

        int k;
        for (k = 0; k < inParamsCount ; k++) {
            paramsNames[k] =  paramsNodes[k]->text;
        }

        Env *global = globalEnv(env);
        Env *envNew = newEnv(newHashTable(100), global, env);
        Region *rNew = make_region(0, NULL);

	int statusInitEnv;
	/* printEnvToStdOut(envNew->current); */
        statusInitEnv = initializeEnv(ruleHead->subtrees[0],args,argc, envNew->current,rNew);
        if (statusInitEnv != 0)
            return newErrorRes(r, statusInitEnv);
        /* printEnvToStdOut(envNew->current); */
        Res *res = evaluateExpression3(ruleCondition, 0, 0, rei, reiSaveFlag,  envNew, errmsg, rNew);
        /* todo consolidate every error into T_ERROR except OOM */
        if (res->nodeType != N_ERROR && TYPE(res)==T_BOOL && res->value.dval!=0) {
#ifndef DEBUG
#if 0
            if (reTestFlag > 0) {
                    if (reTestFlag == COMMAND_TEST_1)
                            fprintf(stdout,"+Executing Rule Number:%i for Action:%s\n",ruleInx,ruleName);
                    else if (reTestFlag == HTML_TEST_1)
                            fprintf(stdout,"+Executing Rule Number:<FONT COLOR=#FF0000>%i</FONT> for Action:<FONT COLOR=#0000FF>%s</FONT><BR>\n",ruleInx,ruleName);
                    else
                            rodsLog (LOG_NOTICE,"+Executing Rule Number:%i for Action:%s\n",ruleInx,ruleName);
            }
#endif
#endif
            if(ruleAction->nodeType == N_ACTIONS) {
                statusRes = evaluateActions(ruleAction, ruleRecovery, rei, reiSaveFlag,  envNew, errmsg,rNew);
            } else {
                statusRes = evaluateExpression3(ruleAction, 0, 0, rei, reiSaveFlag, envNew, errmsg, rNew);
            }
            /* copy parameters */
            copyFromEnv(args, paramsNames, inParamsCount, envNew->current, r);
            /* copy return values */
            statusRes = cpRes(statusRes, r);

            if (statusRes->nodeType == N_ERROR) {
                    #ifndef DEBUG
                    rodsLog (LOG_NOTICE,"applyRule Failed for action 2: %s with status %i",ruleHead->text, statusRes->value.errcode);
                    #endif
            }
        } else {
            if(res->nodeType!=N_ERROR && TYPE(res)!=T_BOOL) {
                char buf[ERR_MSG_LEN];
                generateErrMsg("error: the rule condition does not evaluate to a boolean value", ruleCondition->expr, ruleCondition->base, buf);
                addRErrorMsg(errmsg, TYPE_ERROR, buf);
            }
            statusRes = newErrorRes(r, RULE_FAILED_ERR);
        }
        /* copy global variables */
        cpEnv(global, r);
        deleteEnv(envNew, 2);
        region_free(rNew);
        return statusRes;

}

Res* matchPattern(Node *pattern, Node *val, Env *env, ruleExecInfo_t *rei, int reiSaveFlag, rError_t *errmsg, Region *r) {
    char errbuf[ERR_MSG_LEN];
    char *localErrorMsg = NULL;
    Node *p = pattern, *v = val;
    if(pattern->nodeType == N_APPLICATION) {
        char matcherName[MAX_NAME_LEN];
        matcherName[0]='~';
        strcpy(matcherName+1, pattern->subtrees[0]->text);
        int ruleInx = -1;
        if(findNextRule2(matcherName, &ruleInx) == 0) {
            v = execRule(matcherName, &val, 1, 0, env, rei, reiSaveFlag, errmsg, r);
            ERROR2(v->nodeType == N_ERROR, "user defined pattern function error");
            if(v->nodeType!=N_TUPLE) {
            	Res **tupleComp = (Res **)region_alloc(r, sizeof(Res *));
            	*tupleComp = v;
            	v = newTupleRes(1, tupleComp ,r);
            }
        } else {
            ERROR2(v->text == NULL || strcmp(pattern->subtrees[0]->text, v->text)!=0, "pattern mismatch constructor");
            Res **tupleComp = (Res **)region_alloc(r, sizeof(Res *) * v->degree);
			memcpy(tupleComp, v->subtrees, sizeof(Res *) * v->degree);
			v = newTupleRes(v->degree, tupleComp ,r);
        }
		Res *res = matchPattern(p->subtrees[1], v, env, rei, reiSaveFlag, errmsg, r);
        return res;
    } else if(pattern->nodeType == TK_VAR) {
        char *varName = pattern->text;
        if(lookupFromEnv(env, varName)==NULL) {
            /* new variable */
            if(insertIntoHashTable(env->current, varName, val) == 0) {
                snprintf(errbuf, ERR_MSG_LEN, "error: unable to write to local variable \"%s\".",varName);
                addRErrorMsg(errmsg, UNABLE_TO_WRITE_LOCAL_VAR, errbuf);
                return newErrorRes(r, UNABLE_TO_WRITE_LOCAL_VAR);
            }
        } else {
                updateInEnv(env, varName, val);
        }
        return newIntRes(r, 0);

    } else if (pattern->nodeType == N_TUPLE) {
    	ERROR2(v->nodeType != N_TUPLE, "pattern mismatch value is not a tuple.");
    	ERROR2(p->degree != v->degree, "pattern mismatch arity");
		int i;
		for(i=0;i<p->degree;i++) {
			Res *res = matchPattern(p->subtrees[i], v->subtrees[i], env, rei, reiSaveFlag, errmsg, r);
			if(res->nodeType == N_ERROR) {
				return res;
			}
		}
		return newIntRes(r, 0);
    }
    ERROR2(1, "malformatted pattern error");
    error:
        generateErrMsg(localErrorMsg,pattern->expr, pattern->base, errbuf);
        addRErrorMsg(errmsg, PATTERN_NOT_MATCHED, errbuf);
        return newErrorRes(r, PATTERN_NOT_MATCHED);

}

            /** this allows use of cut as the last msi in the body so that other rules will not be processed
                even when the current rule succeeds **/
/*            if (cutFlag == 1)
       		return(CUT_ACTION_ON_SUCCESS_PROCESSED_ERR);
*/

