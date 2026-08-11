import{describe,expect,it}from'vitest';import{findingsBySeverity,shouldHighlight}from'./logic';import type{Finding}from'./types';
const finding={id:'1',severity:'critical',failure_mechanism:'torque',affected_entities:['motor','arm-joint'],data:{}} satisfies Finding;
describe('finding presentation',()=>{it('groups severity without altering results',()=>{expect(findingsBySeverity([finding]).critical).toEqual([finding])});it('highlights affected geometry',()=>{expect(shouldHighlight('motor',[finding])).toBe(true);expect(shouldHighlight('base',[finding])).toBe(false)})})
