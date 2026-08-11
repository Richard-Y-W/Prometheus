import type{Finding}from'./types';
export const findingsBySeverity=(items:Finding[])=>items.reduce<Record<string,Finding[]>>((groups,item)=>{(groups[item.severity]??=[]).push(item);return groups},{})
export const shouldHighlight=(partId:string,items:Finding[])=>items.some(item=>item.severity==='critical'&&item.affected_entities.some(entity=>entity===partId||entity.startsWith(partId)))
