export type Part={id:string;name:string;type:string;badge:string;shape:'box'|'cylinder';position:number[];scale:number[];children?:Part[]}
export type Finding={id:string;severity:string;failure_mechanism:string;affected_entities:string[];data:Record<string,any>}
