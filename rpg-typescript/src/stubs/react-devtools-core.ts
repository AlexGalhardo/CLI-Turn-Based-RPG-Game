/**
 * Build-time stand-in for Ink's optional `react-devtools-core` peer dependency.
 * Ink only loads it when `DEV=true`; without this stub `bun build --compile` fails to resolve the import.
 */
const devtools = {
	initialize(): void {},
	connectToDevTools(): void {},
};

export default devtools;
