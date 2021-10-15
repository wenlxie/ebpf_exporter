package exporter

import (
	"fmt"
	bpf "github.com/aquasecurity/libbpfgo"
)

// attach attaches functions to tracing points in provided module
func attach(module *bpf.Module, kprobes, kretprobes, tracepoints, rawTracepoints map[string]string) ([]string, error) {
    f := []string{}

	probes, err := attachSomething(module, kprobes,"kprobe");
	if err != nil {
		return nil, fmt.Errorf("failed to attach kprobes: %s", err)
	}
	f = append(f,probes...)

	probes, err = attachSomething(module, kretprobes, "kretprobe");
	if err != nil {
		return nil, fmt.Errorf("failed to attach kretprobes: %s", err)
	}
	f = append(f,probes...)

	probes, err = attachSomething(module, tracepoints, "tracepoint");
	if err != nil {
		return nil, fmt.Errorf("failed to attach tracepoints: %s", err)
	}
	f = append(f, probes...)

	probes, err = attachSomething(module, rawTracepoints, "rawtracepoint");
	if err != nil {
		return nil, fmt.Errorf("failed to attach raw tracepoints: %s", err)
	}
	f = append(f, probes...)

	return f, nil
}

// attachSomething attaches some kind of probes and returns program tags
func attachSomething(module *bpf.Module, probes map[string]string, key string) ([]string, error) {
	f := []string{}

	for program, probe := range probes {
		prog, err := module.GetProgram(program)
		if err != nil {
			return nil, fmt.Errorf("Can't get  program:%v err:%v", program, err)
		}
		f = append(f, program)

		switch key {
		case "kprobe":
			_, err = prog.AttachKprobe(probe)
		case "kretprobe":
			_, err = prog.AttachKretprobe(probe)
		case "tracepoint":
			_, err = prog.AttachTracepoint(probe)
		case "rawtracepoint":
			_, err = prog.AttachRawTracepoint(probe)
		}
		if err != nil {
			return nil, fmt.Errorf("Can't attach probe:%v err:%v", probe, err)
		}
	}

	return f, nil
}


