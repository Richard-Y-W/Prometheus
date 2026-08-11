export type Part={id:string;name:string;type:string;badge:string;shape:'box'|'cylinder';position:number[];scale:number[];children?:Part[]}
export type ComponentPackage={id:string;manufacturer:string;part_number:string;model_level:string;validation_status:string;parameters:Record<string,number|number[]>;evidence:Array<Record<string,string>>}
export type Finding={id:string;severity:string;failure_mechanism:string;affected_entities:string[];data:Record<string,any>}
