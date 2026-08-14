import type{Finding}from'./types';
export type DisabledAction='research'|'run';
const disabledExplanations:Record<DisabledAction,string>={
 research:'Component research moved to the versioned Qt review flow; this archived rough-V1 interface cannot review or publish evidence.',
 run:'Engineering execution is disabled until Program 01B connects reviewed execution packages to the authoritative C++ path.'
};
export const disabledActionExplanation=(action:DisabledAction)=>disabledExplanations[action];
export const findingsBySeverity=(items:Finding[])=>items.reduce<Record<string,Finding[]>>((groups,item)=>{(groups[item.severity]??=[]).push(item);return groups},{})
export const shouldHighlight=(partId:string,items:Finding[])=>items.some(item=>item.severity==='critical'&&item.affected_entities.some(entity=>entity===partId||entity.startsWith(partId)))
